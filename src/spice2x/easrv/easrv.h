#pragma once

void easrv_start(unsigned short port, bool maintenance, int backlog, int thread_count);
void easrv_shutdown();
