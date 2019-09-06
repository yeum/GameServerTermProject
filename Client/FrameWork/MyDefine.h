#pragma once
#include "MyInclude.h"
#define SAFE_DELETE(p) {if(p){ delete(p); (p) = NULL;}}
#define SAFE_DELETE_ARRAY(p) {if(p){ delete[](p); (p) = NULL;}}
#define BUF_SIZE 1024
#define SERVER_IP "127.0.0.1"
#define	WM_SOCKET WM_USER + 1