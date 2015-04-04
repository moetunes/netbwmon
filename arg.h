#ifndef ARG_H__
#define ARG_H__

#define ARG_BEGIN \
    char *argv0; \
    for (argv0 = *argv, argv++, argc--;\
                argv[0] && argv[0][1] && argv[0][0] == '-';\
                argc--, argv++) {\
        char argc_;\
        char **argv_;\
        int brk_;\
        if (argv[0][1] == '-' && argv[0][2] == '\0') {\
            argv++;\
            argc--;\
            break;\
        }\
        for (brk_ = 0, argv[0]++, argv_ = argv;\
             argv[0][0] && !brk_; argv[0]++) {\
                if (argv_ != argv)\
                    break;\
                argc_ = argv[0][0];\
                switch (argc_)

#define ARG_END }}

#define ARGC() argc_

#define ARG_NUMF(val,base)\
    (brk_ = 1, estrtol(argv[0],(val),(base)))

#define ARG_EF(x) \
    ((argv[0][1] == '\0' && argv[1] == NULL)?\
        ((x), abort(), (char *)0) :\
        (brk_ = 1, (argv[0][1] != '\0')?\
                (&argv[0][1]) :\
                (argc--, argv++, argv[0])))

#define ARG_F()\
    ((argv[0][1] == '\0' && argv[1] == NULL)?\
        (char *)0 :\
        (brk_ = 1, (argv[0][1] != '\0')?\
                (&argv[0][1]) :\
                (argc--, argv++, argv[0])))

#endif
