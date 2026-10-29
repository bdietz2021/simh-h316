#pragma once
static void send_json_regs(char jsonbuf[256]);

void super_get_input_event();

void h316_save(struct register_set *, unsigned int P, unsigned int A, unsigned int B, unsigned int X);

// struct options {
//   int option1;
//   int option2;
//   int debug;
// };
// extern struct options debug_opt;  // global flags for debugging
