#include "atheme.h"

#include "syn.h"

DECLARE_MODULE_V1
(
        "syn/tor", false, _modinit, _moddeinit,
        "$Revision$",
        "Stephen Bennett <stephen -at- freenode.net>"
);

static void tor_kline_check(void *);
static void tor_newuser(hook_user_data_t *data);

static void syn_cmd_checktor(sourceinfo_t*, int, char**);

command_t syn_checktor = { "CHECKTOR", N_("Checks for tor nodes on a given IP."), "syn:general", 1, syn_cmd_checktor };

static void load_tor_list();

mowgli_patricia_t *torlist;

unsigned int kline_duration = 24 * 3600;
char *kline_reason;

void _modinit(module_t *m)
{
    user_t *u;
    mowgli_patricia_iteration_state_t state;

    use_syn_main_symbols(m);
    use_syn_kline_symbols(m);

    command_add(&syn_checktor, syn_cmdtree);

    hook_add_event("user_add");
    hook_add_user_add(tor_newuser);
    hook_add_event("syn_kline_check");
    hook_add_hook("syn_kline_check", tor_kline_check);

    event_add("update_tor_list", load_tor_list, NULL, 120);

    load_tor_list();

    kline_reason = sstrdup("Tor access to freenode is hidden service only. Mail kline@freenode.net with questions.");

    add_dupstr_conf_item("TOR_KLINE_REASON", syn_conftable, &kline_reason);
    add_uint_conf_item("TOR_KLINE_DURATION", syn_conftable, &kline_duration, 1, (unsigned int)-1);

    MOWGLI_PATRICIA_FOREACH(u, &state, userlist)
    {
        hook_user_data_t data = { .u = u };
        tor_newuser(&data);
    }
}

void _moddeinit()
{
    mowgli_patricia_destroy(torlist, NULL, NULL);

    del_conf_item("TOR_KLINE_DURATION", syn_conftable);
    del_conf_item("TOR_KLINE_REASON", syn_conftable);

    command_delete(&syn_checktor, syn_cmdtree);

    hook_del_user_add(tor_newuser);
    hook_del_hook("syn_kline_check", tor_kline_check);

    event_delete(load_tor_list, NULL);
}

static void tor_newuser(hook_user_data_t *data)
{
    user_t *u = data->u;

    if (is_internal_client(u) || *u->ip == '\0')
        return;

    syn_kline_check_data_t d = { u->ip, u };
    tor_kline_check(&d);
}

static void tor_kline_check(void *v)
{
    syn_kline_check_data_t *d = v;

    if (!d->ip)
        return;

    if (NULL == mowgli_patricia_retrieve(torlist, d->ip))
        return;

    // IP was listed in the tor list.
    syn_report("K:lining tor node %s (user %s)", d->ip, d->u->nick);
    syn_kline(d->ip, kline_duration, kline_reason);
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
    fclose(f);

    // List might have new entries. Check them.
    user_t *u;
    mowgli_patricia_iteration_state_t state;
    MOWGLI_PATRICIA_FOREACH(u, &state, userlist)
    {
        hook_user_data_t data = { .u = u };
        tor_newuser(&data);
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

