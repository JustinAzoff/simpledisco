#ifndef __GATEWAY_H_INCLUDED__
#define __GATEWAY_H_INCLUDED__

int server_cmd(char *bind);
int keygen_cmd(const char *keypair_filename);
int gateway_cmd (char *node_name);

#endif
