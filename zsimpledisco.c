#include "czmq_library.h"

//  --------------------------------------------------------------------------
//  The self_t structure holds the state for one actor instance

typedef struct {
    zsock_t *pipe;              //  Actor command pipe
    bool terminated;            //  Did caller ask us to quit?
    bool verbose;               //  Verbose logging enabled?
    zsock_t *server_socket;     //  Socket for talking to clients

} self_t;

static void
s_self_destroy (self_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        self_t *self = *self_p;
        if (self->server_socket) // don't close STDIN
            zsock_destroy (&self->server_socket);
        freen (self);
        *self_p = NULL;
    }
}

static self_t *
s_self_new (zsock_t *pipe)
{
    self_t *self = (self_t *) zmalloc (sizeof (self_t));
    assert (self);
    self->pipe = pipe;

    self->server_socket = zsock_new (ZMQ_ROUTER);

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
    if (streq (command, "BIND")) {
        char *endpoint = zstr_recv (self->pipe);
        if(-1 == zsock_bind (self->server_socket, "%s", endpoint))
            zsys_warning ("could not bind to %s", endpoint);
        zstr_free(&endpoint);
    }
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
    s_self_destroy(&self);
}
