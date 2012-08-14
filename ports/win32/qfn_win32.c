/*****************************************************************************
* Product: QF-nano emulation for Win32
* Last Updated for Version: 4.5.02
* Date of the Last Update:  Jun 29, 2012
*
*                    Q u a n t u m     L e a P s
*                    ---------------------------
*                    innovating embedded systems
*
* Copyright (C) 2002-2012 Quantum Leaps, LLC. All rights reserved.
*
* This program is open source software: you can redistribute it and/or
* modify it under the terms of the GNU General Public License as published
* by the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*
* Alternatively, this program may be distributed and modified under the
* terms of Quantum Leaps commercial licenses, which expressly supersede
* the GNU General Public License and are specifically designed for
* licensees interested in retaining the proprietary status of their code.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*
* Contact information:
* Quantum Leaps Web sites: http://www.quantum-leaps.com
*                          http://www.state-machine.com
* e-mail:                  info@quantum-leaps.com
*****************************************************************************/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>                                           /* Win32 API */

#include "qpn_port.h"                                       /* QP-nano port */
#ifdef QK_PREEMPTIVE
    #error "This QP-nano port does not support QK_PREEMPTIVE configuration"
#endif

Q_DEFINE_THIS_MODULE("qfn_win32")

/**
* \file
* \ingroup qfn
* QF-nano emulation for Win32.
*/

/* Global objects ----------------------------------------------------------*/
uint8_t volatile QF_readySet_;                      /* ready-set of QF-nano */

#ifdef Q_TIMERSET
uint8_t volatile QF_timerSet_;                      /* timer-set of QF-nano */
#endif

/* local objects -----------------------------------------------------------*/
static CRITICAL_SECTION l_win32CritSect;          /* Win32 critical section */
static HANDLE  l_win32Event;    /* Win32 event to signal when AOs are ready */
static uint8_t l_running;

static uint8_t const Q_ROM Q_ROM_VAR l_pow2Lkup[] = {
    (uint8_t)0x00,
    (uint8_t)0x01, (uint8_t)0x02, (uint8_t)0x04, (uint8_t)0x08,
    (uint8_t)0x10, (uint8_t)0x20, (uint8_t)0x40, (uint8_t)0x80
};
static uint8_t const Q_ROM Q_ROM_VAR l_invPow2Lkup[] = {
    (uint8_t)0xFF,
    (uint8_t)0xFE, (uint8_t)0xFD, (uint8_t)0xFB, (uint8_t)0xF7,
    (uint8_t)0xEF, (uint8_t)0xDF, (uint8_t)0xBF, (uint8_t)0x7F
};
static uint8_t l_running;

/*..........................................................................*/
void QF_enterCriticalSection(void) {
    EnterCriticalSection(&l_win32CritSect);
}
/*..........................................................................*/
void QF_leaveCriticalSection(void) {
    LeaveCriticalSection(&l_win32CritSect);
}

/*..........................................................................*/
#if (Q_PARAM_SIZE != 0)
void QActive_post(QActive * const me, QSignal sig, QParam par)
#else
void QActive_post(QActive * const me, QSignal sig)
#endif
{
    QActiveCB const Q_ROM *acb = &QF_active[me->prio];

            /* the queue must be able to accept the event (cannot overflow) */
    Q_ASSERT(me->nUsed < Q_ROM_BYTE(acb->end));

    QF_INT_DISABLE();
                                /* insert event into the ring buffer (FIFO) */
    QF_ROM_QUEUE_AT_(acb, me->head).sig = sig;
#if (Q_PARAM_SIZE != 0)
    QF_ROM_QUEUE_AT_(acb, me->head).par = par;
#endif
    if (me->head == (uint8_t)0) {
        me->head = Q_ROM_BYTE(acb->end);                   /* wrap the head */
    }
    --me->head;
    ++me->nUsed;
    if (me->nUsed == (uint8_t)1) {              /* is this the first event? */
        QF_readySet_ |= Q_ROM_BYTE(l_pow2Lkup[me->prio]);    /* set the bit */
#ifdef QK_PREEMPTIVE
        sig = QK_schedPrio_();          /* reuse 'sig' to hold the priority */
        if (sig != (QSignal)0) {
            QK_sched_(sig);             /* check for synchronous preemption */
        }
#endif
        SetEvent(l_win32Event);
    }
    QF_INT_ENABLE();
}
/*..........................................................................*/
#if (Q_PARAM_SIZE != 0)
void QActive_postISR(QActive * const me, QSignal sig, QParam par)
#else
void QActive_postISR(QActive * const me, QSignal sig)
#endif
{
#ifdef QF_ISR_NEST
#ifdef QF_ISR_STAT_TYPE
    QF_ISR_STAT_TYPE stat;
#endif
#endif
    QActiveCB const Q_ROM *acb = &QF_active[me->prio];

            /* the queue must be able to accept the event (cannot overflow) */
    Q_ASSERT(me->nUsed < Q_ROM_BYTE(acb->end));

#ifdef QF_ISR_NEST
#ifdef QF_ISR_STAT_TYPE
    QF_ISR_DISABLE(stat);
#else
    QF_INT_DISABLE();
#endif
#endif
                                /* insert event into the ring buffer (FIFO) */
    QF_ROM_QUEUE_AT_(acb, me->head).sig = sig;
#if (Q_PARAM_SIZE != 0)
    QF_ROM_QUEUE_AT_(acb, me->head).par = par;
#endif
    if (me->head == (uint8_t)0) {
        me->head = Q_ROM_BYTE(acb->end);                   /* wrap the head */
    }
    --me->head;
    ++me->nUsed;
    if (me->nUsed == (uint8_t)1) {              /* is this the first event? */
        QF_readySet_ |= Q_ROM_BYTE(l_pow2Lkup[me->prio]);    /* set the bit */
        SetEvent(l_win32Event);
    }

#ifdef QF_ISR_NEST
#ifdef QF_ISR_STAT_TYPE
    QF_ISR_ENABLE(stat);
#else
    QF_INT_ENABLE();
#endif
#endif
}

/*--------------------------------------------------------------------------*/
#if (QF_TIMEEVT_CTR_SIZE != 0)

/*..........................................................................*/
void QF_tickISR(void) {
    uint8_t p = (uint8_t)QF_MAX_ACTIVE;
    do {
        QActive *a = QF_ROM_ACTIVE_GET_(p);
        if (a->tickCtr != (QTimeEvtCtr)0) {
            --a->tickCtr;
            if (a->tickCtr == (QTimeEvtCtr)0) {
#ifdef Q_TIMERSET
                QF_timerSet_ &= Q_ROM_BYTE(l_invPow2Lkup[p]);  /* clear bit */
#endif

#if (Q_PARAM_SIZE != 0)
                QActive_postISR(a, (QSignal)Q_TIMEOUT_SIG, (QParam)0);
#else
                QActive_postISR(a, (QSignal)Q_TIMEOUT_SIG);
#endif
            }
        }
        --p;
    } while (p != (uint8_t)0);
}

/*..........................................................................*/
void QActive_arm(QActive * const me, QTimeEvtCtr const tout) {
    QF_INT_DISABLE();
    me->tickCtr = tout;
#ifdef Q_TIMERSET
    QF_timerSet_ |= Q_ROM_BYTE(l_pow2Lkup[me->prio]);        /* set the bit */
#endif
    QF_INT_ENABLE();
}
/*..........................................................................*/
void QActive_disarm(QActive * const me) {
    QF_INT_DISABLE();
    me->tickCtr = (QTimeEvtCtr)0;
#ifdef Q_TIMERSET
    QF_timerSet_ &= Q_ROM_BYTE(l_invPow2Lkup[me->prio]);       /* clear bit */
#endif
    QF_INT_ENABLE();
}

#endif                                    /* #if (QF_TIMEEVT_CTR_SIZE != 0) */

/*..........................................................................*/
void QF_stop(void) {
    l_running = (uint8_t)0;
    SetEvent(l_win32Event);   /* unblock the event-loop so it can terminate */
}

/*--------------------------------------------------------------------------*/
int16_t QF_run(void) {
    static uint8_t const Q_ROM Q_ROM_VAR log2Lkup[] = {
        (uint8_t)0, (uint8_t)1, (uint8_t)2, (uint8_t)2,
        (uint8_t)3, (uint8_t)3, (uint8_t)3, (uint8_t)3,
        (uint8_t)4, (uint8_t)4, (uint8_t)4, (uint8_t)4,
        (uint8_t)4, (uint8_t)4, (uint8_t)4, (uint8_t)4
    };
    uint8_t p;
    QActive *a;

    InitializeCriticalSection(&l_win32CritSect);
    l_win32Event = CreateEvent(NULL, FALSE, FALSE, NULL);

                         /* set priorities all registered active objects... */
    for (p = (uint8_t)1; p <= (uint8_t)QF_MAX_ACTIVE; ++p) {
        a = QF_ROM_ACTIVE_GET_(p);
        Q_ASSERT(a != (QActive *)0);    /* QF_active[p] must be initialized */
        a->prio = p;               /* set the priority of the active object */
    }
         /* trigger initial transitions in all registered active objects... */
    for (p = (uint8_t)1; p <= (uint8_t)QF_MAX_ACTIVE; ++p) {
        a = QF_ROM_ACTIVE_GET_(p);
#ifndef QF_FSM_ACTIVE
        QHsm_init(&a->super);         /* take the initial transition in HSM */
#else
        QFsm_init(&a->super);         /* take the initial transition in FSM */
#endif
    }

    QF_onStartup();   /* invoke startup callback (create the ticker thread) */

    l_running = (uint8_t)1;
    while (l_running) {             /* the event loop of the vanilla kernel */
        QF_INT_DISABLE();
        if (QF_readySet_ != (uint8_t)0) {
            QActiveCB const Q_ROM *acb;

#if (QF_MAX_ACTIVE > 4)
            if ((QF_readySet_ & 0xF0) != 0U) {        /* upper nibble used? */
                p = (uint8_t)(Q_ROM_BYTE(log2Lkup[QF_readySet_ >> 4])
                              + (uint8_t)4);
            }
            else                    /* upper nibble of QF_readySet_ is zero */
#endif
            {
                p = Q_ROM_BYTE(log2Lkup[QF_readySet_]);
            }
            acb = &QF_active[p];
            a = QF_ROM_ACTIVE_GET_(p);
            Q_ASSERT(a->nUsed > (uint8_t)0);/*some events must be available */
            --a->nUsed;
            if (a->nUsed == (uint8_t)0) {          /* queue becoming empty? */
                QF_readySet_ &= Q_ROM_BYTE(l_invPow2Lkup[p]);  /* clear bit */
            }
            Q_SIG(a) = QF_ROM_QUEUE_AT_(acb, a->tail).sig;
#if (Q_PARAM_SIZE != 0)
            Q_PAR(a) = QF_ROM_QUEUE_AT_(acb, a->tail).par;
#endif
            if (a->tail == (uint8_t)0) {                    /* wrap around? */
                a->tail = Q_ROM_BYTE(acb->end);
            }
            --a->tail;
            QF_INT_ENABLE();

#ifndef QF_FSM_ACTIVE
            QHsm_dispatch(&a->super);                    /* dispatch to HSM */
#else
            QFsm_dispatch(&a->super);                    /* dispatch to FSM */
#endif
        }
        else {
            QF_onIdle();
                                 /* yield the CPU until new event(s) arrive */
            WaitForSingleObject(l_win32Event, (DWORD)INFINITE);
        }
    }
    QF_onCleanup();                                     /* cleanup callback */
    //CloseHandle(l_win32Event);
    //DeleteCriticalSection(&l_win32CritSect);

    return (int16_t)0;                                           /* success */
}