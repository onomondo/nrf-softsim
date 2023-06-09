#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <nrf_modem.h>
#include <nrf_modem_at.h>
#include <pm_config.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <nrf_softsim.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#define PAYLOAD_SIZE 100

static const struct device *const uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

/**@brief Recoverable modem library error. */
void nrf_modem_recoverable_error_handler(uint32_t err) {
  printk("Modem library recoverable error: %u\n", err);
}

static void lte_handler(const struct lte_lc_evt *const evt);
static int server_connect(void);

K_SEM_DEFINE(lte_connected, 0, 1);
K_SEM_DEFINE(profile_received, 0, 1);

static int client_fd;
static struct sockaddr_storage host_addr;
static struct k_work_delayable server_transmission_work;

static void server_transmission_work_fn(struct k_work *work) {
  int err;
  char buffer[] = "{\"message\":\"Hello from Onomondo!\"}";
  err = send(client_fd, buffer, sizeof(buffer) - 1, 0);
  if (err < 0) {
    printk("Failed to transmit TCP packet, %d\n", errno);

    k_work_schedule(&server_transmission_work, K_SECONDS(2));

    return;
  }

  k_work_schedule(&server_transmission_work, K_SECONDS(20));
}

static void work_init(void) {
  k_work_init_delayable(&server_transmission_work, server_transmission_work_fn);
}

static void lte_handler(const struct lte_lc_evt *const evt) {
  switch (evt->type) {
  case LTE_LC_EVT_NW_REG_STATUS:
    if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
        (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
      break;
    }

    printk("Network registration status: %s\n",
           evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME
               ? "Connected - home network"
               : "Connected - roaming\n");
    k_sem_give(&lte_connected);
    break;
  case LTE_LC_EVT_PSM_UPDATE:
    printk("PSM parameter update: TAU: %d, Active time: %d\n", evt->psm_cfg.tau,
           evt->psm_cfg.active_time);
    break;
  case LTE_LC_EVT_EDRX_UPDATE: {
    char log_buf[60];
    ssize_t len;

    len = snprintf(log_buf, sizeof(log_buf),
                   "eDRX parameter update: eDRX: %f, PTW: %f\n",
                   evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
    if (len > 0) {
      printk("%s\n", log_buf);
    }
    break;
  }
  case LTE_LC_EVT_RRC_UPDATE:
    printk("RRC mode: %s\n",
           evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle\n");
    break;
  case LTE_LC_EVT_CELL_UPDATE:
    printk("LTE cell changed: Cell ID: %d, Tracking area: %d\n", evt->cell.id,
           evt->cell.tac);
    break;
  default:
    break;
  }
}

static void modem_connect(void) {
  int err;

  err = lte_lc_connect_async(lte_handler);
  if (err) {
    printk("Connecting to LTE network failed, error: %d\n", err);
    return;
  }
}
static void server_disconnect(void) { (void)close(client_fd); }

static int server_init(void) {
  struct sockaddr_in *server4 = ((struct sockaddr_in *)&host_addr);

  server4->sin_family = AF_INET;
  server4->sin_port = htons(4321);

  inet_pton(AF_INET, "1.2.3.4", &server4->sin_addr);

  return 0;
}

static int server_connect(void) {
  int err;

  client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (client_fd < 0) {
    printk("Failed to create TCP socket: %d\n", errno);
    err = -errno;
    goto error;
  }

  err = connect(client_fd, (struct sockaddr *)&host_addr,
                sizeof(struct sockaddr_in));
  if (err < 0) {
    printk("Connect failed : %d\n", errno);
    goto error;
  }

  return 0;

error:
  server_disconnect();

  return err;
}

struct rx_buf_t {
  char *buf;
  size_t len;
  size_t pos;
};

void serial_cb(const struct device *dev, void *user_data) {
  struct rx_buf_t *rx = (struct rx_buf_t *)user_data;
  char *rx_buf = rx->buf;
  size_t *rx_buf_pos = &rx->pos;
  size_t rx_buf_len = rx->len;

  if (!uart_irq_update(uart_dev)) {
    return;
  }

  while (uart_irq_rx_ready(uart_dev)) {
    *rx_buf_pos += uart_fifo_read(uart_dev, &rx_buf[*rx_buf_pos],
                                  rx_buf_len - *rx_buf_pos);

    if (*rx_buf_pos == rx_buf_len) {
      k_sem_give(&profile_received);
    }
  }
}

void main(void) {
  int32_t err;
  printk("SoftSIM sample started.\n");

  if (!nrf_sofsim_check_provisioned()) {

    if (!device_is_ready(uart_dev)) {
      printk("UART device not found!");
      return;
    }

    char *profile_read_from_external_source = k_malloc(332);
    assert(profile_read_from_external_source != NULL);

    struct rx_buf_t rx = {
        .buf = profile_read_from_external_source,
        .len = 332,
        .pos = 0,
    };

    uart_irq_callback_user_data_set(uart_dev, serial_cb, &rx);
    uart_irq_rx_enable(uart_dev);

    do {
      printk("Waiting for profile...\n");
    } while (k_sem_take(&profile_received, K_SECONDS(10)));

    uart_irq_rx_disable(uart_dev);

    printk("Provisioning profile...\n");
    nrf_sofsim_provision((uint8_t *)profile_read_from_external_source, rx.pos);

    if (profile_read_from_external_source != NULL) {
      k_free(profile_read_from_external_source);
    }
  }

  err = lte_lc_init();
  if (err) {
    printk("Failed to initialize nrf link control, err %d\n", err);
    return;
  }

  work_init();

  /* Software SIM selection */
  err = nrf_modem_at_printf("AT%%CSUS=2");
  if (err) {
    printk("Failed to select sofsim, err %d\n", err);
    return;
  }

  err = nrf_modem_at_printf("AT+CFUN=41");
  if (err) {
    printk("Failed to activate uicc, err %d\n", err);
    return;
  }

  modem_connect();
  do {
    printk("Waiting for LTE connect event.\n");

  } while (k_sem_take(&lte_connected, K_SECONDS(10)));
  err = server_init();
  if (err) {
    printk("Not able to initialize TCP server connection\n");
    return;
  }

  err = server_connect();
  if (err) {
    printk("Not able to connect to TCP server\n");
    return;
  }

  k_work_schedule(&server_transmission_work, K_NO_WAIT);
}
