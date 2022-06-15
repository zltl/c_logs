#define ROOT_DIR "example"
#include "log.h"

int main() {
    INFOF("this is info, int=%d, float=%f, string=%s", 1, 1.1, "hello");
    ERRORF("this is error");
    return 0;
}
