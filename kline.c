#include "atheme.h"
#include "pmodule.h"

#include "syn.h"

DECLARE_MODULE_V1
(
        "syn/kline", false, _modinit, _moddeinit,
        "$Revision$",
        "Stephen Bennett <stephen -at- freenode.net>"
);

mowgli_patricia_t *ircd_klines;
list_t ircd_wildcard_klines;
BlockHeap *ircd_kline_heap;

static void syn_m_kline(sourceinfo_t *si, int parc, char **parv);
static void syn_m_unkline(sourceinfo_t *si, int parc, char **parv);
static void expire_klines(void *unused);

char *kline_kill_reason = 0;

void _modinit(module_t *m)
{
    use_syn_main_symbols(m);

    ircd_klines = mowgli_patricia_create(noopcanon);
    ircd_kline_heap = BlockHeapCreate(sizeof(kline_t), 16);

    hook_add_event("syn_kline_added");

    kline_kill_reason = sstrdup("Banned");
    add_dupstr_conf_item("KLINE_KILL_REASON", syn_conftable, &kline_kill_reason);

    pcommand_add("KLINE", syn_m_kline, 5, MSRC_USER);
    pcommand_add("UNKLINE", syn_m_unkline, 3, MSRC_USER);

    event_add("expire_ircd_klines", expire_klines, NULL, 120);
}

void _moddeinit()
{
    pcommand_delete("KLINE");
    pcommand_delete("UNKLINE");
    event_delete(expire_klines, NULL);
}

kline_t* _syn_find_kline(const char *user, const char *host)
{
    kline_t *k;
    if ((k = mowgli_patricia_retrieve(ircd_klines, host)))
        return k;

    node_t *n;
    LIST_FOREACH(n, ircd_wildcard_klines.head)
    {
        if (match(k->host, host))
            return k;
    }
    return NULL;
}

static void syn_m_kline(sourceinfo_t *si, int parc, char **parv)
{
    if (_syn_find_kline(parv[2], parv[3]))
    {
        syn_debug(3, "Duplicate K:line %s@%s", parv[2], parv[3]);
        return;
    }

    kline_t *k = BlockHeapAlloc(ircd_kline_heap);
    k->duration = atoi(parv[1]);
    k->settime = CURRTIME;
    k->expires = CURRTIME + k->duration;
    k->user = sstrdup(parv[2]);
    k->host = sstrdup(parv[3]);
    k->reason = sstrdup(parv[4]);

    char *p;
    if (NULL != (p = strchr(k->reason, '|')))
    {
        *p = '\0';
    }

    if (strchr(k->host, '*') || strchr(k->host, '?'))
    {
        node_t *n = node_create();
        node_add(k, n, &ircd_wildcard_klines);
    }
    else
    {
        mowgli_patricia_add(ircd_klines, k->host, k);
    }

    hook_call_event("syn_kline_added", k);

    syn_debug(1, "Added K:line %s@%s (%s)", k->user, k->host, k->reason);
}

static void syn_m_unkline(sourceinfo_t *si, int parc, char **parv)
{
    node_t *n, *tn;
    kline_t *k;

    const char *user = parv[1], *host = parv[2];

    kline_t *removed = mowgli_patricia_delete(ircd_klines, host);
    if (removed)
        BlockHeapFree(ircd_kline_heap, removed);

    LIST_FOREACH_SAFE(n, tn, ircd_wildcard_klines.head)
    {
        k = (kline_t*) n->data;
        if (0 == strcasecmp(k->user, user) &&
            0 == strcasecmp(k->host, host))
        {
            syn_debug(1, "Removing K:line on %s@%s", k->user, k->host);
            node_del(n, &ircd_wildcard_klines);
            free(k->user);
            free(k->host);
            free(k->reason);
            BlockHeapFree(ircd_kline_heap, k);
        }
    }
}

static void expire_klines(void *unused)
{
    node_t *n, *tn;
    kline_t *k;

    LIST_FOREACH_SAFE(n, tn, ircd_wildcard_klines.head)
    {
        k = (kline_t*) n->data;

        if (k->duration == 0)
            continue;

        if (k->expires <= CURRTIME)
        {
            syn_debug(1, "Expiring K:line on %s@%s", k->user, k->host);
            node_del(n, &ircd_wildcard_klines);
            free(k->user);
            free(k->host);
            free(k->reason);
            BlockHeapFree(ircd_kline_heap, k);
        }
    }

    mowgli_patricia_iteration_state_t state;
    MOWGLI_PATRICIA_FOREACH(k, &state, ircd_klines)
    {
        if (k->duration == 0)
            continue;
        if (k->expires <= CURRTIME)
        {
            syn_debug(1, "Expiring K:line on %s@%s", k->user, k->host);
            mowgli_patricia_delete(ircd_klines, k->host);
            free(k->user);
            free(k->host);
            free(k->reason);
            BlockHeapFree(ircd_kline_heap, k);
        }
    }
}

static void _syn_vkline(const char *host, int duration, const char *reason, va_list ap)
{
    if (_syn_find_kline("*", host))
        return;

    char buf[BUFSIZE];
    vsnprintf(buf, BUFSIZE, reason, ap);

    kline_t *k = BlockHeapAlloc(ircd_kline_heap);
    k->duration = duration;
    k->settime = CURRTIME;
    k->expires = CURRTIME + duration;
    k->user = sstrdup("*");
    k->host = sstrdup(host);
    k->reason = sstrdup(buf);

    char *p;
    if (NULL != (p = strchr(k->reason, '|')))
    {
        *p = '\0';
    }

    if (strchr(k->host, '*') || strchr(k->host, '?'))
    {
        node_t *n = node_create();
        node_add(k, n, &ircd_wildcard_klines);
    }
    else
    {
        mowgli_patricia_add(ircd_klines, k->host, k);
    }

    kline_sts("*", "*", k->host, k->duration, k->reason);

    hook_call_event("syn_kline_added", k);

    syn_debug(1, "Added K:line %s@%s (%s)", k->user, k->host, k->reason);
}

void _syn_kline(const char *host, int duration, const char *reason, ...)
{
    va_list ap;
    va_start(ap, reason);
    _syn_vkline(host, duration, reason, ap);
    va_end(ap);
}

static void _syn_vkill(user_t *victim, const char *reason, va_list ap)
{
    char buf[BUFSIZE];
    vsnprintf(buf, BUFSIZE, reason, ap);
    notice(syn->nick, victim->nick, buf);
    kill_user(syn->me, victim, "%s", kline_kill_reason);
}


void _syn_kill(user_t *victim, const char *reason, ...)
{
    va_list ap;

    va_start(ap, reason);
    _syn_vkill(victim, reason, ap);
    va_end(ap);
}

static void _syn_vkill2(user_t *victim, const char *killreason, const char *reason, va_list ap)
{
    char buf[BUFSIZE];
    vsnprintf(buf, BUFSIZE, reason, ap);
    notice(syn->nick, victim->nick, buf);
    kill_user(syn->me, victim, "%s", killreason);
}


void _syn_kill2(user_t *victim, const char *killreason, const char *reason, ...)
{
    va_list ap;

    va_start(ap, reason);
    _syn_vkill2(victim, killreason, reason, ap);
    va_end(ap);
}

void _syn_kill_or_kline(user_t *victim, int duration, const char *reason, ...)
{
    va_list ap;
    va_start(ap, reason);
    if (victim->ip[0] == '\0')
    {
        // No IP means an auth spoofed user, probably a gateway. Kill it instead.
        // Don't give away the oper reason, though.
        char user_reason[BUFSIZE];
        strncpy(user_reason, reason, sizeof(user_reason));
        char *pipe;
        if (0 != (pipe = strchr(user_reason, '|')))
            *pipe = '\0';

        _syn_vkill(victim, user_reason, ap);
    }
    else
    {
        _syn_vkline(victim->ip, duration, reason, ap);
    }
    va_end(ap);
}


