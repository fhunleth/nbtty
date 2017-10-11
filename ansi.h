#ifndef ANSI_H
#define ANSI_H

int ansi_process_input(const unsigned char *input, size_t input_size,
                   unsigned char *output, size_t *output_size, struct winsize *ws);
int ansi_size_request(int fd);
void ansi_reset_parser();

#endif // ANSI_H
