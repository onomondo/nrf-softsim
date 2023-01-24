#include <init.h>
#include <logging/log.h>
#include <nrf_modem_os.h>
#include <nrf_modem_softsim.h>
#include <nrf_softsim.h>
#include <onomondo/softsim/softsim.h>
#include <onomondo/softsim/utils.h>
#include <zephyr.h>

static void softsim_req_task(struct k_work *item);

LOG_MODULE_REGISTER(nrf_softsim, LOG_LEVEL_ERR);

#define SOFTSIM_STACK_SIZE \
    10000  // TODO: this is too much. Figure out some more reasonable value.
#define SOFTSIM_PRIORITY 5  // TODO: What is a good balence here?
K_THREAD_STACK_DEFINE(softsim_stack_area, SOFTSIM_STACK_SIZE);

struct k_work_q softsim_work_q;
static K_FIFO_DEFINE(softsim_req_fifo);
static K_WORK_DEFINE(softsim_req_work, softsim_req_task);
static uint8_t softsim_buffer_out[SIM_HAL_MAX_LE];

struct ss_context *ctx = NULL;

struct softsim_req_node {
    void *fifo_reserved;
    enum nrf_modem_softsim_uicc_req req;
    uint16_t req_id;
    struct nrf_modem_softsim_payload payload;
};

static void softsim_req_task(struct k_work *item);

int onomondo_init(void) {
    k_work_queue_init(&softsim_work_q);

    k_work_queue_start(&softsim_work_q, softsim_stack_area,
                       K_THREAD_STACK_SIZEOF(softsim_stack_area),
                       SOFTSIM_PRIORITY, NULL);

    if (!ctx) {
        ctx = ss_new_ctx();
    }

    ss_reset(ctx);
    LOG_INF("SoftSIM initialized");
    return 0;
}

__weak void nrf_modem_softsim_reset_handler(void) { LOG_INF("SoftSIM RESET"); }

static void softsim_req_task(struct k_work *item) {
    int err;
    int ret;
    struct softsim_req_node *s_req;
    while ((s_req = k_fifo_get(&softsim_req_fifo, K_NO_WAIT))) {
        switch (s_req->req) {
            case INIT: {
                LOG_INF("SoftSIM INIT");

                struct nrf_modem_softsim_payload payload_out = {
                    .data = softsim_buffer_out, .data_len = SIM_HAL_MAX_LE};

                if (!ctx) {
                    ctx = ss_new_ctx();
                }

                ss_reset(ctx);

                int len = ss_atr(ctx, payload_out.data, payload_out.data_len);

                payload_out.data_len = len;
                err = nrf_softsim_init(0, s_req->req_id, &payload_out);

                if (err) {
                    LOG_ERR("SoftSIM INIT response failed with err: %d", err);
                }

                break;
            }
            case APDU: {
                LOG_DBG("SoftSIM APDU");

                struct nrf_modem_softsim_payload payload_out = {
                    .data = softsim_buffer_out, .data_len = SIM_HAL_MAX_LE};

                struct nrf_modem_softsim_payload payload_in = s_req->payload;

                uint16_t req_len = payload_in.data_len;
                size_t rsp_len = ss_command_apdu_transact(
                    ctx, payload_out.data, payload_out.data_len,
                    payload_in.data, req_len);

                payload_out.data_len = rsp_len;

                err = nrf_softsim_apdu(0, s_req->req_id, &payload_out);
                if (err) {
                    LOG_ERR("SoftSIM APDU response failed with err: %d", err);
                }

                nrf_modem_os_free(s_req->payload.data);

                break;
            }
            case DEINIT: {
                LOG_INF("SoftSIM DEINIT");

                ss_free_ctx(ctx);

                // TODO: FS technically don't have to be mounted anymore.

                err = nrf_softsim_deinit(0, s_req->req_id);

                if (err) {
                    LOG_ERR("SoftSIM DEINIT response failed with err: %d", err);
                }

                break;
            }
            case RESET: {
                LOG_INF("SoftSIM RESET");

                ss_reset(ctx);

                err = nrf_softsim_reset(s_req->req_id);

                if (err) {
                    LOG_ERR("SoftSIM RESET response failed with err: %d", err);
                }

                break;
            }
            default:
                break;
        }
        nrf_modem_os_free(s_req);
    }
}

void nrf_modem_os_softsim_defer_req(enum nrf_modem_softsim_uicc_req req,
                                    uint16_t req_id, uint16_t data_len,
                                    uint8_t const *data) {
    struct softsim_req_node *req_node = NULL;
    void *payload_data = NULL;

    req_node = nrf_modem_os_alloc(sizeof(struct softsim_req_node));

    if (data_len != 0) {
        payload_data = nrf_modem_os_alloc(data_len);
    }

    if (req_node &&
        ((data_len != 0 && payload_data) || (data_len == 0 && !payload_data))) {
        if (data_len != 0) {
            memcpy(payload_data, data, data_len);
        }

        req_node->req = req;
        req_node->req_id = req_id;
        req_node->payload.data = payload_data;
        req_node->payload.data_len = data_len;

        k_fifo_put(&softsim_req_fifo, req_node);
        k_work_submit_to_queue(&softsim_work_q, &softsim_req_work);
    } else {
        LOG_ERR("Couldn't allocate SoftSIM deferred request.");
    }
}

#ifdef CONFIG_SOFTSIM_AUTO_INIT
SYS_INIT(onomondo_init, APPLICATION, 0);
#endif