#include "stm32f4xx.h"

/* Passive monitor: PB3=SCK, PB5=MOSI, PB13=D/C, PB15=CS, PB12=RST.
 * No MISO connection. PB14 stays high to deselect the board's SPI Flash.
 * Capture starts only after observing the TFT's hardware reset low then high.
 * SPI samples MOSI in hardware on rising edges (mode 0, MSB first).
 * D/C, CS and RST are sampled by software immediately after RXNE; they are
 * diagnostic hints near byte boundaries, NOT synchronous edge captures.
 */
#define CAPTURE_CAPACITY 50000U
#define RST_MASK (1U << 12)
#define MONITOR_MASK ((1U << 3) | (1U << 5) | (1U << 12) | (1U << 13) | (1U << 15))

enum capture_state {
    CAPTURE_BOOT = 0,
    CAPTURE_WAIT_RESET_LOW = 1,
    CAPTURE_WAIT_RESET_HIGH = 2,
    CAPTURE_RECEIVING = 3,
    CAPTURE_FULL = 4,
    CAPTURE_TIMEOUT = 5,
    CAPTURE_OVERRUN = 6,
    CAPTURE_RESET_DURING_RX = 7,
    CAPTURE_SPI_FAULT = 8
};

/* Record: bits 7:0=data, 8=D/C, 9=CS, 10=RST (GPIO after RXNE).
 * Only g_capture_count entries are valid. Buffer freezes at terminal state.
 * Watch/MCP should read the finished buffer, not halt during reception.
 */
volatile uint16_t g_capture_records[CAPTURE_CAPACITY];
volatile uint32_t g_capture_count;
volatile enum capture_state g_capture_state;
volatile uint32_t g_capture_last_sr;
volatile uint32_t g_capture_last_gpio;
volatile uint32_t g_capture_clock_hz;
volatile uint32_t g_capture_elapsed_cycles;

static void monitor_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;

    /* Preload Flash CS high before configuring PB14 as an output. */
    GPIOB->BSRRL = (1U << 14);
    GPIOB->MODER &= ~((3U << 6) | (3U << 10) | (3U << 24) |
                      (3U << 26) | (3U << 28) | (3U << 30));
    GPIOB->MODER |= (2U << 6) | (2U << 10) | (1U << 28);
    GPIOB->OTYPER &= ~(1U << 14);
    GPIOB->PUPDR &= ~((3U << 6) | (3U << 10) | (3U << 24) |
                      (3U << 26) | (3U << 28) | (3U << 30));
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~((15U << 12) | (15U << 20))) |
                    (5U << 12) | (5U << 20);

    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;
    RCC->APB2RSTR |= RCC_APB2RSTR_SPI1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_SPI1RST;
    /* Slave, receive-only, 8-bit, CPOL=0, CPHA=0, software NSS low.
     * Ignore external CS for byte assembly; record its state separately.
     * This also captures stray clocks while CS is high for diagnosis.
     */
    SPI1->CR1 = SPI_CR1_RXONLY | SPI_CR1_SSM;
    SPI1->CR2 = 0U;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

int main(void)
{
    uint32_t start;
    uint32_t count = 0U;
    uint32_t timeout;

    __disable_irq(); /* No interrupt-driven services in this capture firmware. */
    SystemCoreClockUpdate();
    g_capture_clock_hz = SystemCoreClock;
    timeout = SystemCoreClock * 10U;
    monitor_init();
    g_capture_state = CAPTURE_WAIT_RESET_LOW;
    while ((GPIOB->IDR & RST_MASK) != 0U) { }
    g_capture_state = CAPTURE_WAIT_RESET_HIGH;
    while ((GPIOB->IDR & RST_MASK) == 0U) { }

    SPI1->CR1 |= SPI_CR1_SPE;
    start = DWT->CYCCNT;
    g_capture_state = CAPTURE_RECEIVING;
    while (count < CAPTURE_CAPACITY)
    {
        uint32_t sr = SPI1->SR;
        if ((sr & (SPI_SR_OVR | SPI_SR_MODF)) != 0U)
        {
            g_capture_last_sr = sr;
            g_capture_state = ((sr & SPI_SR_OVR) != 0U) ?
                              CAPTURE_OVERRUN : CAPTURE_SPI_FAULT;
            break;
        }
        if ((sr & SPI_SR_RXNE) != 0U)
        {
            uint32_t gpio = GPIOB->IDR;
            uint32_t value = SPI1->DR;
            g_capture_records[count] = (uint16_t)((value & 255U) |
                (((gpio >> 13) & 1U) << 8) |
                (((gpio >> 15) & 1U) << 9) |
                (((gpio >> 12) & 1U) << 10));
            ++count;
            g_capture_count = count;
        }
        if ((GPIOB->IDR & RST_MASK) == 0U)
        {
            g_capture_state = CAPTURE_RESET_DURING_RX;
            break;
        }
        if ((uint32_t)(DWT->CYCCNT - start) >= timeout)
        {
            g_capture_state = CAPTURE_TIMEOUT;
            break;
        }
    }
    g_capture_elapsed_cycles = DWT->CYCCNT - start;
    g_capture_last_sr = SPI1->SR;
    g_capture_last_gpio = GPIOB->IDR & MONITOR_MASK;
    SPI1->CR1 &= ~SPI_CR1_SPE;
    if (count == CAPTURE_CAPACITY) { g_capture_state = CAPTURE_FULL; }
    for (;;) { __NOP(); }
}
