c_logs - A logging library implement by C
---

c_logs is a logging library implement by C.

# Usage

`log.c`, `log.h`, `sstr.c`, `sstr.h` should be droped into your project and
compiled along with it. Use example:

```C
INFOF("this is info, int=%d, float=%f, string=%s", 1, 1.1, "hello");
ERRORF("this is error");
```

Resulting in a lines with the given format like printf to stdout or specified log file.

```
2022-06-15T09:43:33.227213+0800 info  example/example.c+5:main this is info, int=1, float=1.1, string=hello
2022-06-15T09:43:33.228531+0800 error example/example.c+6:main this is error
```
