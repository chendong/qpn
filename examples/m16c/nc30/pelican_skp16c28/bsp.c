/*****************************************************************************
* Product: PELICAN crossing example, M16C/SKP16C28 board, Vanilla kernel
* Last Updated for Version: 4.4.00
* Date of the Last Update:  Mar 02, 2012
*
*                    Q u a n t u m     L e a P s
*                    ---------------------------
*                    innovating embedded systems
*
* Copyright (C) 2002-2012 Quantum Leaps, LLC. All rights reserved.
*
* This software may be distributed and modified under the terms of the GNU
* General Public License version 2 (GPL) as published by the Free Software
* Foundation and appearing in the file GPL.TXT included in the packaging of
* this file. Please note that GPL Section 2[b] requires that all works based
* on this software must also be made publicly available under the terms of
* the GPL ("Copyleft").
*
* Alternatively, this software may be distributed and modified under the
* terms of Quantum Leaps commercial licenses, which expressly supersede
* the GPL and are specifically designed for licensees interested in
* retaining the proprietary status of their code.
*
* Contact information:
* Quantum Leaps Web site:  http://www.quantum-leaps.com
* e-mail:                  info@quantum-leaps.com
*****************************************************************************/
#include "qpn_port.h"
#include "pelican.h"
#include "bsp.h"
#include "lcd.h"
                                                       /* clock frequencies */
#define f1_CLK_HZ           (2*MAIN_OSC_HZ)
#define fc_CLK_HZ           (SUB_OSC_HZ)

                 /* priority of the time-tick ISR, (1-lowest ... 7-highest) */
#define TICK_ISR_PRIO       1

/*..........................................................................*/
#pragma INTERRUPT ta0_isr (vect = 21)              /* system clock tick ISR */
void ta0_isr(void) {
    static uint8_t btn_debounced  = 0;
    static uint8_t debounce_state = 0;
    uint8_t btn;

    QF_INT_ENABLE();                                          /* see NOTE01 */

    QF_tick();                             /* process all armed time events */

    btn = SW1;                                  /* read the user switch SW1 */
    switch (debounce_state) {
        case 0:
            if (btn != btn_debounced) {
                debounce_state = 1;         /* transition to the next state */
            }
            break;
        case 1:
            if (btn != btn_debounced) {
                debounce_state = 2;         /* transition to the next state */
            }
            else {
                debounce_state = 0;           /* transition back to state 0 */
            }
            break;
        case 2:
            if (btn != btn_debounced) {
                btn_debounced = btn;     /* save the debounced button value */

                if (btn == 0) {                 /* is the button depressed? */
                    QActive_postISR((QActive *)&AO_Pelican,
                                    PEDS_WAITING_SIG, 0);
                }
            }
            debounce_state = 0;               /* transition back to state 0 */
            break;
    }

    QF_INT_DISABLE();                                         /* see NOTE01 */
}
/*..........................................................................*/
void BSP_init(void) {
    uint16_t volatile delay;
                                                 /* initialize the clock... */
    prc1 = 1;                  /* enable access to processor mode registers */
    pm20 = 0;       /* 2 wait states for SFR access for >16MHz operation... */
    prc1 = 0;                 /* disable access to processor mode registers */

                        /* configure PLL, must be consistent with f1_CLK_HZ */
    prc0 = 1;                           /* enable access to clock registers */
    cm1  = 0x20;                        /* set to hi-drive Xin, divide by 1 */
    cm0  = 0x08;                     /* set to main clock using divide by 1 */
    cm21 = 0;                                              /* switch to Xin */
    plc0 = 0x11;                                     /* configure PLL to x2 */
    plc0 = 0x91;                                              /* enable PLL */
    for (delay = 20000; delay != 0; --delay) {       /* wait for stable PLL */
    }
    cm11 = 1;                                              /* switch to PLL */
    prc0 = 0;                          /* disable access to clock registers */

                                                    /* enable the User LEDs */
    LED0_DDR = 1;
    LED1_DDR = 1;
    LED2_DDR = 1;
    LED0 = LED_OFF;
    LED1 = LED_OFF;
    LED2 = LED_OFF;

                                       /* confgure Switch pins as inputs... */
    SW1_DDR = 0;
    SW2_DDR = 0;
    SW3_DDR = 0;

    InitDisplay();                          /* initialize and clear the LCD */

                                /* configure 32kHz sub clock..., see NOTE02 */
    pd8_7 = 0;                           /* set GPIO to inputs (XCin/XCout) */
    pd8_6 = 0;
    prc0 = 1;                                         /* unlock CM0 and CM1 */
    cm04 = 1;                                    /* start the 32KHz crystal */
    prc0 = 0;                     /* lock the System Clock Control Register */

                                      /* setup Timer A running from fc32... */
    ta0mr = 0xC0;                      /* Timer mode, fc32, no pulse output */
    ta0   = (int)((fc_CLK_HZ/32 + BSP_TICKS_PER_SEC/2)
                  / BSP_TICKS_PER_SEC) - 1;                       /* period */
    ta0ic = TICK_ISR_PRIO;   /* set the clock tick interrupt priority level */
}
/*..........................................................................*/
void QF_onStartup(void) {
    ta0s = 1;                                             /* Start timer A0 */
}
/*..........................................................................*/
void QF_onCleanup(void) {
}
/*..........................................................................*/
void QF_onIdle(void) {
#ifdef NDEBUG                   /* low-power mode interferes with debugging */
    /* stop all peripheral clocks that you can in your applicaiton ... */
    _asm("FSET I");            /* NOTE: the following WAIT instruction will */
    _asm("WAIT");          /* execute before entering any pending interrupt */
#else
    QF_INT_ENABLE();                              /* just enable interrupts */
#endif
}
/*..........................................................................*/
void BSP_signalCars(enum BSP_CarsSignal sig) {
    switch (sig) {
        case CARS_RED: {
            LED0 = LED_ON;
            LED1 = LED_OFF;
            LED2 = LED_OFF;
            break;
        }
        case CARS_YELLOW: {
            LED0 = LED_OFF;
            LED1 = LED_ON;
            LED2 = LED_OFF;
            break;
        }
        case CARS_GREEN: {
            LED0 = LED_OFF;
            LED1 = LED_OFF;
            LED2 = LED_ON;
            break;
        }
        case CARS_OFF: {
            LED0 = LED_OFF;
            LED1 = LED_OFF;
            LED2 = LED_OFF;
            break;
        }
    }
}
/*..........................................................................*/
void BSP_signalPeds(enum BSP_PedsSignal sig) {
    switch (sig) {
        case PEDS_DONT_WALK: {
            DisplayString((uint8_t)(LCD_LINE2 + 0), "DON'T WK");
            break;
        }
        case PEDS_BLANK: {
            DisplayString((uint8_t)(LCD_LINE2 + 0), "        ");
            break;
        }
        case PEDS_WALK: {
            DisplayString((uint8_t)(LCD_LINE2 + 0), "* WALK *");
            break;
        }
    }
}
/*..........................................................................*/
void BSP_showState(uint8_t prio, char const *state) {
    if (prio == 1) {                              /* PELICAN active object? */
        DisplayString((uint8_t)(LCD_LINE1 + 0), state);
    }
    else {                                        /* Operator active object */
    }
}
/*--------------------------------------------------------------------------*/
void Q_onAssert(char const Q_ROM * const Q_ROM_VAR file, int line) {
    QF_INT_DISABLE();                                 /* disable interrupts */
    for (;;) {                                /* hang in this for-ever loop */
    }
}

/*****************************************************************************
* NOTE01:
* The M16C microcontroller supports interrupt prioritization. Therefore it
* is safe to unlock interrupts inside ISRs. By assigning priorities to
* interrupts you have full control over interrupt nesting. In particular,
* you can avoid interrupt nesting by assigning the same priority level to
* all interrupts.
*
* The simple policy of unconditional enabling of interrupts upon exit from
* a critical section precludes nesting of critical sections. This policy
* means that you *must* unlock interrupts inside every ISR before invoking
* any QF service for ISR, such as QActvie_postISR() or QF_tick().
*
* NOTE02:
* The non-preemptive QF port (foreground/background) requires an atomic
* transiton to the low-power WAIT mode of the M16C MCU. As described in
* "M16C Software Manual", Section 5.2.1 "Interrupt Enable Flag", the
* instruction immediately following "FSET I" instruction, will be executed
* before any pending interrupt. This guarantees atomic transition to the
* WAIT mode. CAUTION: The instruction pair (FSET I, WAIT) should never be
* separated by any other instruction.
*/