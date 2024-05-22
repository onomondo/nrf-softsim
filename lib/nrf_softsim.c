#include <autoconf.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ss_profile.h"
#include "ss_crypto.h"
#include <nrf_softsim.h>
#include <nrf_modem_at.h>
#include <nrf_modem_softsim.h>
#include <modem/nrf_modem_lib.h>
#include <onomondo/softsim/softsim.h>
#include <onomondo/softsim/utils.h>
#include <onomondo/softsim/fs_port.h>

LOG_MODULE_REGISTER(softsim, CONFIG_SOFTSIM_LOG_LEVEL);

#define SOFTSIM_STACK_SIZE 10000  // TODO: this is too much. Figure out some more reasonable value.
#define SOFTSIM_PRIORITY 5        // TODO: What is a good balence here?
K_THREAD_STACK_DEFINE(softsim_stack_area, SOFTSIM_STACK_SIZE);

#define SIM_HAL_MAX_LE 260

static void softsim_req_task(struct k_work *item);
int port_provision(struct ss_profile *profile);
int port_check_provisioned(void);

static struct k_work_q softsim_work_q;
static K_FIFO_DEFINE(softsim_req_fifo);
static K_WORK_DEFINE(softsim_req_work, softsim_req_task);
static uint8_t softsim_buffer_out[SIM_HAL_MAX_LE];

// softsim handle
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

static void nrf_modem_softsim_req_handler(enum nrf_modem_softsim_cmd req, uint16_t req_id, void *data,
                                          uint16_t data_len);

int onomondo_init(void)
{
  /**
   * Init here?
   * Pro: pretty smooth :) -> no need to init and deinit more than needed..
   * Con: IF someone actively de-init with the intent of freeing resources,
   * there will be *some* wasted memory.. Actual memory held isn't too much
   * though Pro: With no explicit de-init we protect the flash from excessive
   * writes Con: EF_LOCI might not be comitted to flash on actual power off...
   *
   * Maybe just call fs_commit() of so on de-init.?
   */
  int rc = ss_init_fs();

#ifdef CONFIG_SOFTSIM_STATIC_PROFILE_ENABLE
#pragma message "Using static profile. Only for development!"
  size_t profile_len = strlen(CONFIG_SOFTSIM_STATIC_PROFILE);

  if (!nrf_softsim_check_provisioned()) {
    LOG_INF("Provisioning static profile");
    nrf_softsim_provision((uint8_t *)CONFIG_SOFTSIM_STATIC_PROFILE, profile_len);
  }
#endif

  if (rc) {
    LOG_ERR("FS failed to init..\n");
    return -1;
  }

  if (nrf_modem_softsim_req_handler_set(nrf_modem_softsim_req_handler)) {
    LOG_ERR("SoftSIM fatal error: Virtual Identity And Global Roaming Aborted");
    return -1;
  }

  k_work_queue_init(&softsim_work_q);

  k_work_queue_start(&softsim_work_q, softsim_stack_area, K_THREAD_STACK_SIZEOF(softsim_stack_area), SOFTSIM_PRIORITY,
                     NULL);

  ctx = ss_new_ctx();  // TODO: consider dropping this call here

  LOG_INF("SoftSIM initialized");
  return 0;
}

// public init
int nrf_softsim_init(void) { return onomondo_init(); }

// public provision api
int nrf_softsim_provision(uint8_t *profile_r, size_t len)
{
  struct ss_profile profile = {0};
  decode_profile(len, profile_r, &profile);

  // import to psa_crypto
  ss_utils_setup_key(KMU_KEY_SIZE, profile.K, KEY_ID_KI);
  ss_utils_setup_key(KMU_KEY_SIZE, profile.KIC, KEY_ID_KIC);
  ss_utils_setup_key(KMU_KEY_SIZE, profile.KID, KEY_ID_KID);

  LOG_INF("SoftSIM keys written to KMU");

  int status = port_provision(&profile);

  if (status != 0) {
    LOG_ERR("SoftSIM failed to update profile");
  } else {
    LOG_INF("SoftSIM fully provisioned");
  }

  return status;
}

int nrf_softsim_check_provisioned(void)
{
  /* Check first PSA key and also first NVS key. */
  return ss_utils_check_key_existence(KEY_ID_KI) && port_check_provisioned();
}

// still needed?
__weak void nrf_modem_softsim_reset_handler(void) { LOG_DBG("SoftSIM RESET"); }

static void softsim_req_task(struct k_work *item)
{
  int err;
  struct softsim_req_node *s_req;

  while ((s_req = k_fifo_get(&softsim_req_fifo, K_NO_WAIT))) {
    switch (s_req->req) {
      case NRF_MODEM_SOFTSIM_INIT: {
        LOG_DBG("SoftSIM INIT REQ");
        if (!ctx) {  // This check is needed since multiple INIT requests can be sent
          ctx = ss_new_ctx();
        }

        if (!ss_is_suspended(ctx)) {
          ss_reset(ctx);
          ss_init_fs();
        }

        int atr_len = ss_atr(ctx, softsim_buffer_out, SIM_HAL_MAX_LE);

        err = nrf_modem_softsim_res(s_req->req, s_req->req_id, softsim_buffer_out, atr_len);

        LOG_HEXDUMP_DBG(softsim_buffer_out, atr_len, "SoftSIM ATR");

        if (err) {
          LOG_ERR("SoftSIM INIT response failed with err: %d", err);
        }

        break;
      }
      case NRF_MODEM_SOFTSIM_APDU: {
        LOG_HEXDUMP_DBG(s_req->payload.data, s_req->payload.data_len, "SoftSIM APDU request");

        size_t req_len = s_req->payload.data_len;
        size_t rsp_len =
            ss_command_apdu_transact(ctx, softsim_buffer_out, SIM_HAL_MAX_LE, s_req->payload.data, &req_len);

        err = nrf_modem_softsim_res(s_req->req, s_req->req_id, softsim_buffer_out, rsp_len);
        if (err) {
          LOG_ERR("SoftSIM APDU response failed with err: %d", err);
        }

        LOG_HEXDUMP_DBG(softsim_buffer_out, rsp_len, "SoftSIM APDU response");
        break;
      }
      case NRF_MODEM_SOFTSIM_DEINIT: {
        LOG_DBG("SoftSIM DEINIT REQ");

        if (ctx && !ss_is_suspended(ctx)) {  // ignore if suspended. Then we just keep the context around
          ss_free_ctx(ctx);
          ctx = NULL;
          ss_deinit_fs();  // Commit any cached changes to flash
        } else {
          LOG_DBG("SoftSIM suspended. Keeping context.");
        }

        err = nrf_modem_softsim_res(s_req->req, s_req->req_id, NULL, 0);
        if (err) {
          LOG_ERR("SoftSIM DEINIT response failed with err: %d", err);
        }

        break;
      }
      case NRF_MODEM_SOFTSIM_RESET: {
        LOG_DBG("SoftSIM RESET");

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

    if (s_req->payload.data) nrf_modem_softsim_data_free(s_req->payload.data);

    k_free(s_req);
  }
}

void nrf_modem_softsim_req_handler(enum nrf_modem_softsim_cmd req, uint16_t req_id, void *data, uint16_t data_len)
{
  struct softsim_req_node *req_node = NULL;

  req_node = k_malloc(sizeof(struct softsim_req_node));

  if (!req_node) {
    LOG_ERR("SoftSIM req_node allocation failed");
    nrf_modem_softsim_err(req, req_id);
    return;  // not good
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

static void ss_on_modem_lib_init(int ret, void *ctx)
{
  int err;

  err = nrf_modem_at_printf("AT%%CSUS=2");
  if (err) {
    LOG_ERR("Failed to select software SIM.");
  }
}
NRF_MODEM_LIB_ON_INIT(sim_select_hook, ss_on_modem_lib_init, NULL);
#endif
