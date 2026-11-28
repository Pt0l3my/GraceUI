/* SPDX-License-Identifier: MIT */
/*
 * hello_space.h: what the fourth example mod registers and says, so that tools/grace-harness can check it through
 * GraceUI's views. Everything about it is invented.
 */
#ifndef HELLO_SPACE_H
#define HELLO_SPACE_H

#define HELLO_SPACE_ID "hello-space"
#define HELLO_SPACE_NAME "Hello space"

/* Its space's page, topics and commands (space/space.toml). */
#define HELLO_SPACE_PAGE "hello.counter"
#define HELLO_SPACE_STATUS "hello.status"
#define HELLO_SPACE_LOG "hello.log"
#define HELLO_SPACE_ADD "hello.add"
#define HELLO_SPACE_RESET "hello.reset"
#define HELLO_SPACE_STEP "hello.step"
#define HELLO_SPACE_LOG_CHANGES "hello.log_changes"

/* What its status says before anything has changed the count, with the step at its default. */
#define HELLO_SPACE_FIRST_SENTENCE "The count is 0, and nothing has changed it yet. Add puts 1 on it."

/* Its own refusals, in its own words (the result's detail). A step out of its bounds: GraceUI leaves a setting's type
 * and bounds to the space, which checks them. A reset that did not ask first: GraceUI refuses that itself, since the
 * page marks Reset destructive, so the mod's own check is a second one, for a command from anywhere. */
#define HELLO_SPACE_STEP_REFUSED "The step is a whole number from 1 to 10."
#define HELLO_SPACE_CONFIRM_FIRST "Reset asks first: the count would go back to 0."

#endif /* HELLO_SPACE_H */
