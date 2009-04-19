#include "atheme.h"

DECLARE_MODULE_V1
(
        "syn/tor", false, _modinit, _moddeinit,
        "$Revision$",
        "Stephen Bennett <stephen -at- freenode.net>"
);

static void tor_newuser(void*);

static void syn_cmd_checktor(sourceinfo_t*, int, char**);

command_t syn_checktor = { "CHECKTOR", N_("Checks for tor nodes on a given IP."), "syn:general", 1, syn_cmd_checktor };

static void load_tor_list();

list_t *syn_cmdtree;
list_t *syn_helptree;

mowgli_patricia_t *torlist;

const int kline_duration = 24 * 3600;
char *kline_reason = "Tor access to freenode is hidden service only. Mail kline@freenode.net with questions.";

void _modinit(module_t *m)
{
    user_t *u;
    mowgli_patricia_iteration_state_t state;

    MODULE_USE_SYMBOL(syn_cmdtree, "syn/main", "syn_cmdtree");
    MODULE_USE_SYMBOL(syn_helptree, "syn/main", "syn_helptree");

    command_add(&syn_checktor, syn_cmdtree);

    hook_add_event("user_add");
    hook_add_hook("user_add", tor_newuser);

    event_add("update_tor_list", load_tor_list, NULL, 120);

    load_tor_list();

    MOWGLI_PATRICIA_FOREACH(u, &state, userlist)
    {
        tor_newuser(u);
    }
}

void _moddeinit()
{
    mowgli_patricia_destroy(torlist, NULL, NULL);
    command_delete(&syn_checktor, syn_cmdtree);
    hook_del_hook("user_add", tor_newuser);
    event_delete(load_tor_list, NULL);
}

static void tor_newuser(void *v)
{
    user_t *u = v;
    void *p;

    if (is_internal_client(u) || *u->ip == '\0')
        return;

    p = mowgli_patricia_retrieve(torlist, u->ip);
    if (p == NULL)
        return;

    // IP was listed in the tor list.
    snoop("tor: K:lining tor node %s (user %s)", u->ip, u->nick);
    kline_sts("*", "*", u->ip, kline_duration, kline_reason);
}

static void load_tor_list()
{
    FILE *f;
    char line[BUFSIZE];

    if (torlist)
        mowgli_patricia_destroy(torlist, NULL, NULL);
    torlist = mowgli_patricia_create(noopcanon);

    f = fopen(DATADIR "/tor.list", "r");
    if (!f)
    {
        slog(LG_DEBUG, "load_tor_list(): cannot open tor node list: %s", strerror(errno));
        return;
    }


    while (fgets(line, BUFSIZE, f))
    {
        strip(line);
        mowgli_patricia_add(torlist, line, (void*)1);
    }
}

static void syn_cmd_checktor(sourceinfo_t *si, int parc, char **parv)
{
    if (!parv[0])
    {
        command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "CHECKTOR");
        command_fail(si, fault_needmoreparams, _("Syntax: CHECKTOR <IP|user>"));
        return;
    }

    void *p;
    char *test = parv[0];

    if (strchr(parv[0], '.') == NULL)
    {
        user_t *target_u = user_find_named(parv[0]);
        if (!target_u)
        {
            command_fail(si, fault_nosuch_target, _("\2%s\2 is not online."), parv[0]);
            return;
        }
        test = target_u->ip;
    }

    p = mowgli_patricia_retrieve(torlist, test);

    command_success_nodata(si, _("\2%s\2 is %s as a tor node."), test, p != NULL ? "listed" : "not listed");
}

