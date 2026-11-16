/*
 * BKIT ARM4 - Lab 2 - Exercise 2: Signal Monitoring with a Timeout
 *
 * - A one-shot k_timer simulates a periodic signal. Its callback (ISR) only
 *   gives sig_sem and restarts the timer with the next interval of the
 *   pattern 1 s, 1 s, 4 s, 1 s, 1 s, 4 s, ... (the 4 s gap = a lost signal).
 * - thread_monitor waits for the signal with a 2000 ms timeout:
 *     0       -> "Signal received" + blink Y0 twice (100 ms on / 100 ms off)
 *     -EAGAIN -> "TIMEOUT - signal lost!" + toggle LED3
 * The ISR never prints and never drives a light.
 */
#include <stdio.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define STACK_SIZE  1024
#define PRIORITY    7
#define WAIT_MS     2000

static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios); /* PE4 */
static const struct gpio_dt_spec y0   = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios); /* PE5 */

/* Binary semaphore: "a signal has arrived" */
K_SEM_DEFINE(sig_sem, 0, 1);

/* Intervals between two signals, used in turn by the one-shot timer */
static const uint32_t interval_ms[] = { 1000, 1000, 4000 };

/* Timer callback = ISR: give + restart only (no printf, no GPIO, no waiting) */
static void sig_timer_isr(struct k_timer *timer)
{
    static unsigned int idx;    /* index of the next interval */

    k_sem_give(&sig_sem);

    idx = (idx + 1) % ARRAY_SIZE(interval_ms);
    k_timer_start(timer, K_MSEC(interval_ms[idx]), K_NO_WAIT);   /* one-shot */
}
K_TIMER_DEFINE(sig_timer, sig_timer_isr, NULL);

static void thread_monitor(void *p1, void *p2, void *p3)
{
    gpio_pin_configure_dt(&led3, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&y0, GPIO_OUTPUT_INACTIVE);

    while (1) {
        int ret = k_sem_take(&sig_sem, K_MSEC(WAIT_MS));

        if (ret == 0) {
            printf("[%6u ms] Signal received\n", k_uptime_get_32());
            for (int i = 0; i < 2; i++) {       /* blink Y0 twice */
                gpio_pin_set_dt(&y0, 1);
                k_msleep(100);
                gpio_pin_set_dt(&y0, 0);
                k_msleep(100);
            }
        } else if (ret == -EAGAIN) {
            printf("[%6u ms] TIMEOUT - signal lost!\n", k_uptime_get_32());
            gpio_pin_toggle_dt(&led3);
        }
    }
}
K_THREAD_DEFINE(monitor_tid, STACK_SIZE, thread_monitor, NULL, NULL, NULL, PRIORITY, 0, 0);

int main(void)
{
    printf("Lab 2 - Exercise 2: Signal monitoring with a timeout\n");
    /* One-shot: first signal after 1 s, the ISR restarts the timer itself */
    k_timer_start(&sig_timer, K_MSEC(interval_ms[0]), K_NO_WAIT);
    return 0;
}
