#ifndef SPICETOOLS_CARDIO_RUNNER_H
#define SPICETOOLS_CARDIO_RUNNER_H

extern bool CARDIO_RUNNER_FLIP;
extern bool CARDIO_RUNNER_TOGGLE;

void cardio_runner_start(bool scan_hid);
void cardio_runner_stop();

#endif //SPICETOOLS_CARDIO_RUNNER_H
