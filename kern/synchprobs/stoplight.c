/*
 * Copyright (c) 2001, 2002, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define NOQUAD -1

static struct lock *lock0;
static struct lock *lock1;
static struct lock *lock2;
static struct lock *lock3;
static struct semaphore *entry_sem;


/* Locks the specified quadrant */
static
void
lock_quadrant(int quadrant)
{
	switch (quadrant) {
		case 0:
			lock_acquire(lock0);
			break;
		case 1:
			lock_acquire(lock1);
			break;
		case 2:
			lock_acquire(lock2);
			break;
		case 3:
			lock_acquire(lock3);
			break;
	}
}

/* Releases the specified quadrant */
static
void
unlock_quadrant(int quadrant)
{
	switch (quadrant) {
		case 0:
			lock_release(lock0);
			break;
		case 1:
			lock_release(lock1);
			break;
		case 2:
			lock_release(lock2);
			break;
		case 3:
			lock_release(lock3);
			break;
	}
}

/* Gives the next quadrant (anti-clockwise) */
static
int 
next_quadrant(int quadrant) 
{
	return (quadrant + 3)%4;
}

/* Move to next quadrant (anti-clockwise) */
static
int
move_to_next_quadrant(int next_quad, int curr_quad, uint32_t index) 
{
	lock_quadrant(next_quad);
	inQuadrant(next_quad, index);
	if (curr_quad != NOQUAD) {
		unlock_quadrant(curr_quad);
	}
	return next_quad;
}

/*
 * Called by the driver during initialization.
 */

void
stoplight_init() {
	entry_sem = sem_create("entry sem", 3);
	if (entry_sem == NULL) {
		panic("spotlight init: sem create failed\n");
	}
	lock0 = lock_create("lock0");
	if (lock0 == NULL) {
		panic("spotlight init: sem create failed\n");
	}
	lock1 = lock_create("lock1");
	if (lock1 == NULL) {
		panic("spotlight init: sem create failed\n");
	}
	lock2 = lock_create("lock2");
	if (lock2 == NULL) {
		panic("spotlight init: sem create failed\n");
	}
	lock3 = lock_create("lock3");
	if (lock3 == NULL) {
		panic("spotlight init: sem create failed\n");
	}
}

/*
 * Called by the driver during teardown.
 */

void stoplight_cleanup() {
	sem_destroy(entry_sem);
	lock_destroy(lock0);
	lock_destroy(lock1);
	lock_destroy(lock2);
	lock_destroy(lock3);
}

void
turnright(uint32_t direction, uint32_t index)
{
	int quad = (int)direction;
	P(entry_sem);
	quad = move_to_next_quadrant(quad, NOQUAD, index);
	leaveIntersection(index);
	unlock_quadrant(quad);
	V(entry_sem);
}
void
gostraight(uint32_t direction, uint32_t index)
{
	int quad = (int)direction;
	P(entry_sem);
	quad = move_to_next_quadrant(quad, NOQUAD, index);
	quad = move_to_next_quadrant(next_quadrant(quad), quad, index);
	leaveIntersection(index);
	unlock_quadrant(quad);
	V(entry_sem);
}
void
turnleft(uint32_t direction, uint32_t index)
{
	int quad = (int)direction;
	P(entry_sem);
	quad = move_to_next_quadrant(quad, NOQUAD, index);
	quad = move_to_next_quadrant(next_quadrant(quad), quad, index);
	quad = move_to_next_quadrant(next_quadrant(quad), quad, index);
	leaveIntersection(index);
	unlock_quadrant(quad);
	V(entry_sem);
}

