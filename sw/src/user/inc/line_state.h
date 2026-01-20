#ifndef __LINE_STATE_H_
#define __LINE_STATE_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int8_t line;     // 0..N
    bool   pressed;  // touch pressed (debounced)
} line_state_t;

/** Initialize once before use. Creates the mutex and sets the initial state. */
void line_state_init(void);

/** Copy-out the current state (task context). */
void line_state_get(line_state_t *out_state);

/** Copy-in a full new state (task context). */
void line_state_set(const line_state_t *new_state);

/**
 * Atomically update the state in-place via a user function.
 * Your function may modify *s; the whole operation is mutex-protected.
 */
void line_state_update(void (*fn)(line_state_t *s));

/** Convenience helpers */
void line_state_set_pressed(bool pressed);
int8_t line_state_add_wrap(int8_t delta, int8_t min, int8_t max); // returns new line

/**
 * Change detection: compares the current state to *last_snapshot.
 * If different, overwrites *last_snapshot with the current state and returns true.
 */
bool line_state_changed_since(line_state_t *last_snapshot);

/** Nonblocking try-get/try-set (optional) */
bool line_state_try_get(line_state_t *out_state);
bool line_state_try_set(const line_state_t *new_state);



void line_state_set_init_mode(void);
void line_state_release_init_mode(void);
bool line_state_check_init_mode(void);

void line_state_set_reset_provisioning_mode(void);
void line_state_release_reset_provisioning_mode(void);
bool line_state_check_reset_provisioning_mode(void);

#endif //__LINE_STATE_H_
