
#include <nrf_modem_os.h>
#include <nrf_modem_softsim.h>
#include <nrf_softsim.h>
#include <onomondo/softsim/softsim.h>
#include <onomondo/softsim/utils.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
static void softsim_req_task(struct k_work *item);

LOG_MODULE_REGISTER(nrf_softsim, LOG_LEVEL_ERR);

#define SOFTSIM_STACK_SIZE \
    10000                   // TODO: this is too much. Figure out some more reasonable value.
#define SOFTSIM_PRIORITY 5  // TODO: What is a good balence here?
K_THREAD_STACK_DEFINE(softsim_stack_area, SOFTSIM_STACK_SIZE);

#define SIM_HAL_MAX_LE 260

struct k_work_q softsim_work_q;
static K_FIFO_DEFINE(softsim_req_fifo);
static K_WORK_DEFINE(softsim_req_work, softsim_req_task);
static uint8_t softsim_buffer_out[SIM_HAL_MAX_LE];

struct ss_context *ctx = NULL;

struct payload_t {
    void *data;
    uint16_t data_len;
};

struct softsim_req_node {
    void *fifo_reserved;
    enum nrf_modem_softsim_cmd req;
    uint16_t req_id;
    struct payload_t payload;
};

static void softsim_req_task(struct k_work *item);

static void nrf_modem_softsim_req_handler(enum nrf_modem_softsim_cmd req,
                                          uint16_t req_id, void *data, uint16_t data_len);

static int onomondo_init(const struct device *) {
    if (nrf_modem_softsim_req_handler_set(nrf_modem_softsim_req_handler)) {
        LOG_ERR("SoftSIM fatal error: Virtual Identity And Global Roaming Aborted\n");
        return -1;
    }

    k_work_queue_init(&softsim_work_q);

    k_work_queue_start(&softsim_work_q, softsim_stack_area,
                       K_THREAD_STACK_SIZEOF(softsim_stack_area),
                       SOFTSIM_PRIORITY, NULL);

    ctx = ss_new_ctx();

    // modem should do this actually.
    // ss_reset(ctx);
    LOG_INF("SoftSIM initialized");
    return 0;
}

__weak void nrf_modem_softsim_reset_handler(void) { LOG_INF("SoftSIM RESET"); }

static void softsim_req_task(struct k_work *item) {
    int err;
    struct softsim_req_node *s_req;
    while ((s_req = k_fifo_get(&softsim_req_fifo, K_NO_WAIT))) {
        switch (s_req->req) {
            case NRF_MODEM_SOFTSIM_INIT: {
                LOG_INF("SoftSIM INIT");

                if (!ctx) {
                    ctx = ss_new_ctx();
                }

                ss_reset(ctx);

                int atr_len = ss_atr(ctx, softsim_buffer_out, SIM_HAL_MAX_LE);

                err = nrf_modem_softsim_res(s_req->req, s_req->req_id, softsim_buffer_out, atr_len);
                if (err) {
                    LOG_ERR("SoftSIM INIT response failed with err: %d", err);
                }

                break;
            }
            case NRF_MODEM_SOFTSIM_APDU: {
                LOG_DBG("SoftSIM APDU");

                size_t rsp_len = ss_command_apdu_transact(
                    ctx, softsim_buffer_out, SIM_HAL_MAX_LE,
                    s_req->payload.data, s_req->payload.data_len);

                err = nrf_modem_softsim_res(s_req->req, s_req->req_id, softsim_buffer_out, rsp_len);
                if (err) {
                    LOG_ERR("SoftSIM APDU response failed with err: %d", err);
                }

                break;
            }
            case NRF_MODEM_SOFTSIM_DEINIT: {
                LOG_INF("SoftSIM DEINIT");

                ss_free_ctx(ctx);

                // TODO: FS technically don't have to be mounted anymore.

                err = nrf_modem_softsim_res(s_req->req, s_req->req_id, NULL, 0);
                if (err) {
                    LOG_ERR("SoftSIM DEINIT response failed with err: %d", err);
                }

                break;
            }
            case NRF_MODEM_SOFTSIM_RESET: {
                LOG_INF("SoftSIM RESET");

                ss_reset(ctx);

                err = nrf_modem_softsim_res(s_req->req, s_req->req_id, NULL, 0);

                if (err) {
                    LOG_ERR("SoftSIM RESET response failed with err: %d", err);
                }

                break;
            }
            default:
                break;
        }

        if (s_req->payload.data)
            nrf_modem_softsim_data_free(s_req->payload.data);

        k_free(s_req);
    }
}

void nrf_modem_softsim_req_handler(enum nrf_modem_softsim_cmd req,
                                   uint16_t req_id, void *data, uint16_t data_len) {
    struct softsim_req_node *req_node = NULL;

    req_node = k_malloc(sizeof(struct softsim_req_node));

    if (!req_node) {
        LOG_ERR("SoftSIM req_node allocation failed");
        nrf_modem_softsim_err(req, req_id);
        return;  // probably schedule fatal error report by now
    }

    req_node->payload.data = data;
    req_node->payload.data_len = data_len;
    req_node->req = req;
    req_node->req_id = req_id;

    k_fifo_put(&softsim_req_fifo, req_node);
    k_work_submit_to_queue(&softsim_work_q, &softsim_req_work);
}

#ifdef CONFIG_SOFTSIM_AUTO_INIT
SYS_INIT(onomondo_init, APPLICATION, 0);
#endif
