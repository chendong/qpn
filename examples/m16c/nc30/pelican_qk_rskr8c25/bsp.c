/*****************************************************************************
* Product: PELICAN crossing example, RSK-R8C25 board
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

uint8_t l_led_mask = 0x00;    /* the current state of LEDs for HEW debugger */
uint8_t l_cars_sig = 0xFF;  /* the current signal for Cars for HEW debugger */
uint8_t l_peds_sig = 0xFF;  /* the current signal for Peds for HEW debugger */

enum LedBits {
    RED_LED_BIT = (1 << 0),
    AMB_LED_BIT = (1 << 1),
    GRN_LED_BIT = (1 << 2),
    ORG_LED_BIT = (1 << 3)
};

/*..........................................................................*/
#pragma interrupt timer_re_isr(vect=10)
void timer_re_isr(void){
    static uint8_t btn_debounced  = 0;
    static uint8_t debounce_state = 0;
    uint8_t btn;

    QK_ISR_ENTRY();                 /* inform QK-nano about entering in ISR */

    QF_tick();                             /* process all armed time events */

    btn = SW1;                                   /* read the user switch S1 */
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
                    QActive_post((QActive *)&AO_Pelican, PEDS_WAITING_SIG, 0);
                }
            }
            debounce_state = 0;               /* transition back to state 0 */
            break;
    }

    QK_ISR_EXIT();                   /* inform QK-nano about exiting in ISR */
}
/*..........................................................................*/
void BSP_init(void) {
                                             /* initialize the CPU clock... */
    prcr = 1;                                                /* protect off */
    cm13 = 1;                                                   /* Xin Xout */
    cm15 = 1;                /* XCIN-XCOUT drive capacity select bit : HIGH */
    cm05 = 0;                                                     /* Xin on */
    cm16 = 0;                              /* main clock = No division mode */
    cm17 = 0;                              /* main clock = No division mode */
    cm06 = 0;                                       /* CM16 and CM17 enable */
    asm("nop");                        /* Wait for stable of oscillation... */
    asm("nop");
    asm("nop");
    asm("nop");
    ocd2 = 0;                                          /* main clock change */
    prcr = 0;                                                 /* protect on */

                                    /* configure 32kHz clock..., see NOTE01 */
    prc0  = 1;                                               /* protect off */
    pd4_3 = 0;    /* unlock CM0 and CM1 and set GPIO to inputs (XCin/XCout) */
    pd4_4 = 0;                                                       /* ... */
    cm04  = 1;                                   /* Start the 32KHz crystal */
    prc0  = 0;                                                /* protect on */

                                      /* start Timer RE running from fc4... */
    trecr1 = 0x00;                                         /* stop Timer RE */
    trecr2 = 0x00;       /* output compare mode, interrupt disabled for now */
    trecsr = 0x63;    /* fc4 clock source, no 4-bit counter, output compare */
                                                /* set output compare value */
    tremin = (uint8_t)((fc_CLK_SPEED/4/2 + BSP_TICKS_PER_SEC/2)
                       / BSP_TICKS_PER_SEC - 1);
                                                    /* enable the User LEDs */
    LED0_DDR = 1;
    LED1_DDR = 1;
    LED2_DDR = 1;
    LED3_DDR = 1;
    LED0 = LED_OFF;
    LED1 = LED_OFF;
    LED2 = LED_OFF;
    LED3 = LED_OFF;

                                                  /* enable the User Button */
    SW1_DDR = 0;

                                             /* Configure the LCD module... */
    pd1  = 0x06;                                   /* Set control = outputs */
    prc2 = 1;                                    /* disable PD0 protect bit */
    pd0  = 0x0F;                                      /* Set data = outputs */

    InitialiseDisplay();
}
/*..........................................................................*/
void QF_onStartup(void) {
    comie_trecr2  = 1;          /* enable Timer RE output compare interrupt */
    tstart_trecr1 = 1;                                    /* start Timer RE */
    treic         = 3;                      /* TRE interrupt priority level */
}
/*..........................................................................*/
void QF_onCleanup(void) {
}
/*..........................................................................*/
void QK_onIdle(void) {
#ifdef NDEBUG                   /* low-power mode interferes with debugging */
    /* stop all peripheral clocks that you can in your applicaiton ... */
    _asm("WAIT");          /* execute before entering any pending interrupt */
#endif
}
/*..........................................................................*/
void BSP_signalCars(enum BSP_CarsSignal sig) {
    l_cars_sig = (uint8_t)sig;       /* save in the global for HEW debugger */

    switch (sig) {
        case CARS_OFF:
            l_led_mask &= ~(RED_LED_BIT | AMB_LED_BIT | GRN_LED_BIT);
            break;
        case CARS_RED:
            l_led_mask |= RED_LED_BIT;
            l_led_mask &= ~(AMB_LED_BIT | GRN_LED_BIT);
            break;
        case CARS_YELLOW:
            l_led_mask |= AMB_LED_BIT;
            l_led_mask &= ~(RED_LED_BIT | GRN_LED_BIT);
            break;
        case CARS_GREEN:
            l_led_mask |= GRN_LED_BIT;
            l_led_mask &= ~(RED_LED_BIT | AMB_LED_BIT);
            break;
    }
    LED0 = (((l_led_mask & RED_LED_BIT) != 0) ? LED_ON : LED_OFF);
    LED1 = (((l_led_mask & AMB_LED_BIT) != 0) ? LED_ON : LED_OFF);
    LED2 = (((l_led_mask & GRN_LED_BIT) != 0) ? LED_ON : LED_OFF);
}
/*..........................................................................*/
void BSP_signalPeds(enum BSP_PedsSignal sig) {
    l_peds_sig = (uint8_t)sig;       /* save in the global for HEW debugger */
    switch (sig) {
        case PEDS_DONT_WALK:
            l_led_mask |= ORG_LED_BIT;
            break;
        case PEDS_BLANK:
            l_led_mask &= ~ORG_LED_BIT;
            break;
        case PEDS_WALK:
            l_led_mask &= ~ORG_LED_BIT;
            break;
    }
    LED3 = (((l_led_mask & ORG_LED_BIT) != 0) ? LED_ON : LED_OFF);
}
/*..........................................................................*/
void BSP_showState(uint8_t prio, char const *state) {
    if (prio == 1) {                              /* PELICAN active object? */
        DisplayString(LCD_LINE1, state);
    }
    else {                                        /* Operator active object */
        DisplayString(LCD_LINE2, state);
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
* The low-power 32KHz clock is used for the system clock tick, because this
* clock is *not* stopped in the low-power WAIT mode.
*/