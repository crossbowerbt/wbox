#ifndef ANET_H
#define ANET_H

#define ANET_OK 0
#define ANET_ERR -1
#define ANET_ERR_LEN 256

int anetNonBlock(char *err, int fd);
int anetTcpNoDelay(char *err, int fd);
int anetTcpConnect(char *err, char *addr, int port);
int anetRead(int fd, void *buf, int count);
int anetResolve(char *err, char *host, char *ipbuf);

#endif
