#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/sys_clock.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include <stm32g474xx.h>

#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>

//
// LED
//

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
#define SW0_NODE DT_ALIAS(sw0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

struct k_timer blinky_tm;

struct k_work blinky_wk;

// pisca LED
void blinky_work_fn(struct k_work *blinky_wk) {
	gpio_pin_toggle_dt(&led);
}

// Chama de novo quando acamar o tempo
void blinky_expired_handler(struct k_timer *timer) {
	k_work_submit(&blinky_wk);
}

K_TIMER_DEFINE(blinky_tm, blinky_expired_handler, NULL);

K_WORK_DEFINE(blinky_wk, blinky_work_fn);

//
// BOTÃO
//

struct k_thread keyboard_use_thread;
struct k_work_delayable keyboard_wk;
struct k_fifo button_queue;
int button_pressed;
static struct gpio_callback button_cb_data;
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});

// Callback botão coloca na fila 
void button_pressed_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	k_work_schedule(&keyboard_wk, K_MSEC(50));
}

// Delay do debounce
void keyboard_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	// Verifica o btn depois do debounce
	if (gpio_pin_get_dt(&button) == 1) {
		button_pressed = 0;
		k_fifo_put(&button_queue, &button_pressed);
	}
}

// Mostra info thread atual (todas threads)
void print_thread_info(const struct k_thread *thread, void *user_data) {
	printk("\t%s\n", thread->name);
}

// Quando aperta o btn mostra as tarefas em uso
void keyboard_use(void) {
	gpio_pin_configure_dt(&button, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&button_cb_data, button_pressed_cb, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

	while (1) {
		int *key = (int *)k_fifo_get(&button_queue, K_NO_WAIT);
		if (key != NULL && *key == 0) {
			printk("Tarefas:\n");
			k_thread_foreach(print_thread_info, NULL);
		}

		k_msleep(100);
	}
}

K_FIFO_DEFINE(button_queue);

K_WORK_DELAYABLE_DEFINE(keyboard_wk, keyboard_work_fn);

K_THREAD_DEFINE(keyboard_use_th, 1024, keyboard_use, NULL, NULL, NULL, 7, 0, 0);

//
// ZBUS
//

struct k_sem fft_sem, fft_print_sem;

K_SEM_DEFINE(fft_sem, 1, 1)
K_SEM_DEFINE(fft_print_sem, 1, 1)

struct adc_msg {
	char ready;
};

ZBUS_CHAN_DEFINE(adc_ch,							  /* Name */
				 struct adc_msg,					  /* Message type */
				 NULL,								  /* Validator */
				 NULL,								  /* User data */
				 ZBUS_OBSERVERS(adc_handler_msg_sub), /* observers */
				 ZBUS_MSG_INIT(0)					  /* Initial value {0} */
);
ZBUS_SUBSCRIBER_DEFINE(adc_handler_msg_sub, 3);

//
// SHELL
//

// Mostra as tarefas em execução
static int cmd_tasks(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Tarefas em Execucao:");
	k_thread_foreach(print_thread_info, NULL);
	return 0;
}

// Mostra a pilha de tarefas
static int cmd_stack(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	const struct k_thread *current = k_current_get();

	size_t simple_usage, size = current->stack_info.size;

	k_thread_stack_space_get(current, &simple_usage);

	shell_print(sh, "Uso total da pilha:\n%zu de %zu\n", simple_usage, size);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(my_log,
							   SHELL_CMD(tasks, NULL, "Mostra as tarefas em execucao", cmd_tasks),
							   SHELL_CMD(stack, NULL, "Mostra as tarefas na pilha", cmd_stack),
							   SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(log, &my_log, "Log", NULL);

int main(void)
{
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

	k_timer_start(&blinky_tm, K_MSEC(500), K_MSEC(500));
	return 0;
}
