#ifndef ANSI_H
#define ANSI_H

#include <stdlib.h>
#include <sys/ioctl.h>

#define ANSI_MAX_RESPONSE_LEN 10 /* The max size of the response and +1 the size that could be buffered */

int ansi_process_input(const unsigned char *input, size_t input_size,
                       unsigned char *output, size_t *output_size, struct winsize *ws);
size_t ansi_size_request(unsigned char *dest);
void ansi_reset_parser();

#endif // ANSI_H
