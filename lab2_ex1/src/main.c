/*
 * BKIT ARM4 - Lab 2 - Exercise 1: Three-Light Relay with Binary Semaphores
 *
 * Three threads with the same priority, each one owns exactly one light:
 *   thread_led3 -> LED3 (PE4), thread_y0 -> Y0 (PE5), thread_y1 -> Y1 (PE6)
 * The turn is passed around with three binary semaphores only:
 *   LED3 -> Y0 -> Y1 -> LED3 -> ...
 * Each thread: take its own semaphore -> light ON 500 ms -> light OFF
 *              -> give the semaphore of the next thread.
 * sem_led3 starts at 1 (LED3 has the first turn), the other two start at 0,
 * so exactly one token circulates and two lights are never on together.
 */
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define ON_MS       500
#define STACK_SIZE  1024
#define PRIORITY    7           /* same priority for the three threads */

static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios); /* PE4 */
static const struct gpio_dt_spec y0   = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios); /* PE5 */
static const struct gpio_dt_spec y1   = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios); /* PE6 */

/* Binary semaphores (limit 1): only one of them holds the token */
K_SEM_DEFINE(sem_led3, 1, 1);   /* LED3 goes first */
K_SEM_DEFINE(sem_y0,   0, 1);
K_SEM_DEFINE(sem_y1,   0, 1);

/* One turn: wait for my token, light ON for 500 ms, OFF, pass the token */
static void run_turn(const struct gpio_dt_spec *light, const char *name,
                     struct k_sem *my_sem, struct k_sem *next_sem)
{
    k_sem_take(my_sem, K_FOREVER);          /* blocked until it is my turn */

    printf("[%6u ms] %s\n", k_uptime_get_32(), name);
    gpio_pin_set_dt(light, 1);
    k_msleep(ON_MS);                        /* times MY light only */
    gpio_pin_set_dt(light, 0);

    k_sem_give(next_sem);                   /* pass the turn */
}

static void thread_led3(void *p1, void *p2, void *p3)
{
    gpio_pin_configure_dt(&led3, GPIO_OUTPUT_INACTIVE);
    while (1) {
        run_turn(&led3, "LED3", &sem_led3, &sem_y0);
    }
}

static void thread_y0(void *p1, void *p2, void *p3)
{
    gpio_pin_configure_dt(&y0, GPIO_OUTPUT_INACTIVE);
    while (1) {
        run_turn(&y0, "Y0", &sem_y0, &sem_y1);
    }
}

static void thread_y1(void *p1, void *p2, void *p3)
{
    gpio_pin_configure_dt(&y1, GPIO_OUTPUT_INACTIVE);
    while (1) {
        run_turn(&y1, "Y1", &sem_y1, &sem_led3);
    }
}

K_THREAD_DEFINE(led3_tid, STACK_SIZE, thread_led3, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(y0_tid,   STACK_SIZE, thread_y0,   NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(y1_tid,   STACK_SIZE, thread_y1,   NULL, NULL, NULL, PRIORITY, 0, 0);

int main(void)
{
    printf("Lab 2 - Exercise 1: Three-light relay (binary semaphores)\n");
    return 0;
}
