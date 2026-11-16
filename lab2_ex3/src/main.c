/*
 * BKIT ARM4 - Lab 2 - Exercise 3: Sharing 2 Output Channels among 4 Workers
 *
 * - 2 identical channels (Y0, Y1) are a pool of resources managed by the
 *   counting semaphore pool_sem (initial 2, limit 2): at most 2 workers
 *   can hold a channel at the same time.
 * - ch_mutex protects the table ch_busy[2] while a worker picks a free
 *   channel or releases it (and while LED3 / the log are updated).
 * - One thread function, thread_worker, created 4 times with K_THREAD_DEFINE;
 *   each instance receives its configuration (id, work time, rest time) in p1.
 * - LED3 is ON when no channel is free (k_sem_count_get(&pool_sem) == 0).
 */
#include <stdio.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define STACK_SIZE  1024
#define PRIORITY    7
#define NUM_CH      2

static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios); /* PE4 */
static const struct gpio_dt_spec ch_led[NUM_CH] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),    /* channel 0 = Y0 (PE5) */
    GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),    /* channel 1 = Y1 (PE6) */
};
static const char *const ch_name[NUM_CH] = { "Y0", "Y1" };

K_SEM_DEFINE(pool_sem, NUM_CH, NUM_CH);   /* 2 free channels at start */
K_MUTEX_DEFINE(ch_mutex);                 /* protects ch_busy[] */
static bool ch_busy[NUM_CH];

struct worker_cfg {
    int id;
    int work_ms;
    int rest_ms;
};

static const struct worker_cfg cfg_w1 = { 1, 1000, 500 };
static const struct worker_cfg cfg_w2 = { 2, 1500, 700 };
static const struct worker_cfg cfg_w3 = { 3, 2000, 300 };
static const struct worker_cfg cfg_w4 = { 4, 2500, 900 };

/* LED3 ON <=> no free channel. Called with ch_mutex held. */
static void update_led3(void)
{
    gpio_pin_set_dt(&led3, k_sem_count_get(&pool_sem) == 0);
}

static void thread_worker(void *p1, void *p2, void *p3)
{
    const struct worker_cfg *cfg = p1;
    int ch = -1;

    while (1) {
        /* 1. Request a channel: blocks while both channels are in use */
        uint32_t t_req = k_uptime_get_32();
        k_sem_take(&pool_sem, K_FOREVER);

        /* 2. Pick a free channel (critical section on ch_busy[]) */
        k_mutex_lock(&ch_mutex, K_FOREVER);
        for (int i = 0; i < NUM_CH; i++) {
            if (!ch_busy[i]) {
                ch_busy[i] = true;
                ch = i;
                break;
            }
        }
        gpio_pin_set_dt(&ch_led[ch], 1);
        update_led3();
        printf("[%6u ms] W%d uses channel %s (waited %u ms)\n",
               k_uptime_get_32(), cfg->id, ch_name[ch], k_uptime_get_32() - t_req);
        k_mutex_unlock(&ch_mutex);

        /* 3. Work: the channel light stays on (mutex NOT held while working) */
        k_msleep(cfg->work_ms);

        /* 4. Release the channel */
        k_mutex_lock(&ch_mutex, K_FOREVER);
        gpio_pin_set_dt(&ch_led[ch], 0);
        ch_busy[ch] = false;
        printf("[%6u ms] W%d releases channel %s\n", k_uptime_get_32(), cfg->id, ch_name[ch]);
        k_sem_give(&pool_sem);
        update_led3();
        k_mutex_unlock(&ch_mutex);

        /* 5. Rest */
        k_msleep(cfg->rest_ms);
    }
}

K_THREAD_DEFINE(w1_tid, STACK_SIZE, thread_worker, (void *)&cfg_w1, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(w2_tid, STACK_SIZE, thread_worker, (void *)&cfg_w2, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(w3_tid, STACK_SIZE, thread_worker, (void *)&cfg_w3, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(w4_tid, STACK_SIZE, thread_worker, (void *)&cfg_w4, NULL, NULL, PRIORITY, 0, 0);

int main(void)
{
    gpio_pin_configure_dt(&led3, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&ch_led[0], GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&ch_led[1], GPIO_OUTPUT_INACTIVE);
    printf("Lab 2 - Exercise 3: 2 channels shared by 4 workers\n");
    return 0;
}
