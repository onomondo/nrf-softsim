#include <drivers/clock_control.h>
#include <drivers/clock_control/nrf_clock_control.h>
#include <drivers/uart.h>
#include <modem/lte_lc.h>
#include <net/socket.h>
#include <nrf_modem.h>
#include <nrf_modem_at.h>
#include <nrf_modem_platform.h>
#include <pm_config.h>
#include <power/reboot.h>
#include <stdio.h>
#include <string.h>
#include <zephyr.h>
#define PAYLOAD_SIZE 100

/**@brief Recoverable modem library error. */
void nrf_modem_recoverable_error_handler(uint32_t err) {
    printk("Modem library recoverable error: %u\n", err);
}

static void lte_handler(const struct lte_lc_evt *const evt);
static int server_connect(void);
/* To strictly comply with UART timing, enable external XTAL oscillator */
void enable_xtal(void) {
    struct onoff_manager *clk_mgr;
    static struct onoff_client cli = {};

    clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
    sys_notify_init_spinwait(&cli.notify);
    (void)onoff_request(clk_mgr, &cli);
}

static const nrf_modem_init_params_t init_params = {
    .ipc_irq_prio = NRF_MODEM_NETWORK_IRQ_PRIORITY,
    .shmem.ctrl =
        {
            .base = PM_NRF_MODEM_LIB_CTRL_ADDRESS,
            .size = CONFIG_NRF_MODEM_LIB_SHMEM_CTRL_SIZE,
        },
    .shmem.tx =
        {
            .base = PM_NRF_MODEM_LIB_TX_ADDRESS,
            .size = CONFIG_NRF_MODEM_LIB_SHMEM_TX_SIZE,
        },
    .shmem.rx =
        {
            .base = PM_NRF_MODEM_LIB_RX_ADDRESS,
            .size = CONFIG_NRF_MODEM_LIB_SHMEM_RX_SIZE,
        },
#if CONFIG_NRF_MODEM_LIB_TRACE_ENABLED
    .shmem.trace =
        {
            .base = PM_NRF_MODEM_LIB_TRACE_ADDRESS,
            .size = CONFIG_NRF_MODEM_LIB_SHMEM_TRACE_SIZE,
        },
#endif
};

K_SEM_DEFINE(lte_connected, 0, 1);

static int client_fd;
static struct sockaddr_storage host_addr;
static struct k_work_delayable server_transmission_work;
static char response[64];

static void server_transmission_work_fn(struct k_work *work) {
    int err;
    char buffer[] = "{\"message\":\"Hello from Onomondo!\",\"reading_1\":22.3}";

    char buffer2[] = "{\"event\":\"example\",\"reading_2\":1112.3}";
    static int count = 0;
    ++count;

    // printk("IP address %s, port number %d\n", "1.2.3.4", 4321);
    count == 1 ? printk("%s\n", buffer) : printk("%s\n", buffer2);
    err = count == 1 ? send(client_fd, buffer, sizeof(buffer) - 1, 0)
                     : send(client_fd, buffer2, sizeof(buffer2) - 1, 0);
    if (err < 0) {
        printk("Failed to transmit TCP packet, %d\n", errno);
        if (count < 5) {
            k_work_schedule(&server_transmission_work, K_SECONDS(2));
        }
        return;
    }
    if (count > 1) {
        printk("Disconnecting and detaching. \n");

        close(client_fd);
        return;
    }
    k_work_schedule(&server_transmission_work, K_SECONDS(5));
}

static void work_init(void) {
    k_work_init_delayable(&server_transmission_work,
                          server_transmission_work_fn);
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
            printk("PSM parameter update: TAU: %d, Active time: %d\n",
                   evt->psm_cfg.tau, evt->psm_cfg.active_time);
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
            printk("RRC mode: %s\n", evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED
                                         ? "Connected"
                                         : "Idle\n");
            break;
        case LTE_LC_EVT_CELL_UPDATE:
            printk("LTE cell changed: Cell ID: %d, Tracking area: %d\n",
                   evt->cell.id, evt->cell.tac);
            break;
        default:
            break;
    }
}

static void modem_init(void) {
    int err;

    err = lte_lc_init();
    if (err) {
        printk("Modem initialization failed, error: %d\n", err);
        return;
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

void main(void) {
    int32_t err;
    enable_xtal();

    printk("SoftSIM sample started.\n");

    err = nrf_modem_init(&init_params, NORMAL_MODE);
    modem_init();
    work_init();

    if (err) {
        printk("Failed to initialize nrf modem lib, err %d\n", err);
        return;
    }

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

    k_sem_take(&lte_connected, K_FOREVER);

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
