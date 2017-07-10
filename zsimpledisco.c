#include "czmq_library.h"

//  --------------------------------------------------------------------------
//  The self_t structure holds the state for one actor instance

typedef struct {
    zsock_t *pipe;              //  Actor command pipe
    bool terminated;            //  Did caller ask us to quit?
    bool verbose;               //  Verbose logging enabled?
    char hostname [NI_MAXHOST]; //  Saved host name
} self_t;


static self_t *
s_self_new (zsock_t *pipe)
{
    self_t *self = (self_t *) zmalloc (sizeof (self_t));
    assert (self);
    self->pipe = pipe;
    return self;
}

static int
s_self_handle_pipe (self_t *self)
{
    //  Get just the command off the pipe
    char *command = zstr_recv (self->pipe);
    if (!command)
        return -1;                  //  Interrupted

    if (self->verbose)
        zsys_info ("zsimpledisco: API command=%s", command);

    if (streq (command, "VERBOSE"))
        self->verbose = true;
    else
    if (streq (command, "$TERM"))
        self->terminated = true;
    else {
        zsys_error ("zbeacon: - invalid command: %s", command);
        assert (false);
    }
    zstr_free (&command);
    return 0;
}


void
zsimpledisco (zsock_t *pipe, void *args)
{
    self_t *self = s_self_new (pipe);
    assert (self);
    //  Signal successful initialization
    zsock_signal (pipe, 0);

    zpoller_t *poller = zpoller_new (NULL);
    zpoller_add (poller, zsock_resolve(self->pipe));

    while (!self->terminated) {
        zsock_t *which = (zsock_t *) zpoller_wait (poller, 1000);
        if(which == zsock_resolve(self->pipe))
            s_self_handle_pipe (self);
        zsys_debug ("zsimpledisco: Idle");
    }
}
