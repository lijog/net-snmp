/*
 * Note: this file originally auto-generated by mib2c using
 *       version : 1.12 $ of : mfd-data-access.m2c,v $ 
 *
 * $Id$
 */
/*
 * standard Net-SNMP includes 
 */
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

/*
 * include our parent header 
 */
#include "ipAddressTable.h"


#include "ipAddressTable_data_access.h"

#include <sys/ioctl.h>
#include <errno.h>


/** @defgroup data_access data_access: Routines to access data
 *
 * These routines are used to locate the data used to satisfy
 * requests.
 * 
 * @{
 */
/**********************************************************************
 **********************************************************************
 ***
 *** Table ipAddressTable
 ***
 **********************************************************************
 **********************************************************************/
/*
 * ipAddressTable is subid 34 of ip.
 * Its status is Current.
 * OID: .1.3.6.1.2.1.4.34, length: 8
 */

/**
 * initialization for ipAddressTable data access
 *
 * This function is called during startup to allow you to
 * allocate any resources you need for the data table.
 *
 * @param ipAddressTable_reg
 *        Pointer to ipAddressTable_registration
 *
 * @retval MFD_SUCCESS : success.
 * @retval MFD_ERROR   : unrecoverable error.
 */
int
ipAddressTable_init_data(ipAddressTable_registration_ptr
                         ipAddressTable_reg)
{
    DEBUGMSGTL(("verbose:ipAddressTable:ipAddressTable_init_data",
                "called\n"));

    /*
     * TODO:303:o: Initialize ipAddressTable data.
     */

    return MFD_SUCCESS;
}                               /* ipAddressTable_init_data */

/**
 * container-cached overview
 *
 */
/**
 * check entry for update
 */
static void
_clear_times(ipAddressTable_rowreq_ctx * rowreq_ctx, void *magic)
{
    rowreq_ctx->ipAddressLastChanged = rowreq_ctx->ipAddressCreated = 0;
}

/***********************************************************************
 *
 * cache
 *
 ***********************************************************************/
/**
 * container initialization
 *
 * @param container_ptr_ptr A pointer to a container pointer. If you
 *        create a custom container, use this parameter to return it
 *        to the MFD helper. If set to NULL, the MFD helper will
 *        allocate a container for you.
 * @param  cache A pointer to a cache structure. You can set the timeout
 *         and other cache flags using this pointer.
 *
 *  This function is called at startup to allow you to customize certain
 *  aspects of the access method. For the most part, it is for advanced
 *  users. The default code should suffice for most cases. If no custom
 *  container is allocated, the MFD code will create one for your.
 *
 *  This is also the place to set up cache behavior. The default, to
 *  simply set the cache timeout, will work well with the default
 *  container. If you are using a custom container, you may want to
 *  look at the cache helper documentation to see if there are any
 *  flags you want to set.
 *
 * @remark
 *  This would also be a good place to do any initialization needed
 *  for you data source. For example, opening a connection to another
 *  process that will supply the data, opening a database, etc.
 */
void
ipAddressTable_container_init(netsnmp_container ** container_ptr_ptr,
                              netsnmp_cache * cache)
{
    DEBUGMSGTL(("verbose:ipAddressTable:ipAddressTable_container_init",
                "called\n"));

    if ((NULL == cache) || (NULL == container_ptr_ptr)) {
        snmp_log(LOG_ERR, "bad params to ipAddressTable_container_init\n");
        return;
    }

    /*
     * For advanced users, you can use a custom container. If you
     * do not create one, one will be created for you.
     */
    /*
     * We create a custom container here so we can pre-load it, which
     * will result in all new entries with last changed values. we need
     * to clear those...  We also need to make sure ifIndexes have been
     * assigned...
     */
    *container_ptr_ptr =
        netsnmp_container_find("ipAddressTable:table_container");
    if (NULL != *container_ptr_ptr) {
        ipAddressTable_cache_load(*container_ptr_ptr);
        CONTAINER_FOR_EACH(*container_ptr_ptr,
                           (netsnmp_container_obj_func *) _clear_times,
                           NULL);
    }

    /*
     * TODO:345:A: Set up ipAddressTable cache properties.
     *
     * Also for advanced users, you can set parameters for the
     * cache. Do not change the magic pointer, as it is used
     * by the MFD helper.
     */
    cache->timeout = IPADDRESSTABLE_CACHE_TIMEOUT;      /* seconds */

    /*
     * basically, turn off all automatic cache handling except autoload.
     */
    cache->flags |=
        (NETSNMP_CACHE_DONT_AUTO_RELEASE | NETSNMP_CACHE_DONT_FREE_EXPIRED
         | NETSNMP_CACHE_DONT_FREE_BEFORE_LOAD | NETSNMP_CACHE_AUTO_RELOAD
         | NETSNMP_CACHE_DONT_INVALIDATE_ON_SET);
}                               /* ipAddressTable_container_init */

/**
 * check entry for update
 */
static void
_check_entry_for_updates(ipAddressTable_rowreq_ctx * rowreq_ctx,
                         void **magic)
{
    netsnmp_container *ipaddress_container = magic[0];
    netsnmp_container *to_delete = (netsnmp_container *) magic[1];

    /*
     * check for matching entry using secondary index.
     */
    netsnmp_ipaddress_entry *ipaddress_entry =
        CONTAINER_FIND(ipaddress_container, rowreq_ctx->data);
    if (NULL == ipaddress_entry) {
        DEBUGMSGTL(("ipAddressTable:access", "removing missing entry\n"));

        if (NULL == to_delete) {
            magic[1] = to_delete = netsnmp_container_find("lifo");
            if (NULL == to_delete)
                snmp_log(LOG_ERR, "couldn't create delete container\n");
        }
        if (NULL != to_delete)
            CONTAINER_INSERT(to_delete, rowreq_ctx);
    } else {
        DEBUGMSGTL(("ipAddressTable:access", "updating existing entry\n"));

        /*
         * Check for changes & update
         */
        if (netsnmp_access_ipaddress_entry_update(rowreq_ctx->data,
                                                  ipaddress_entry) > 0)
            rowreq_ctx->ipAddressLastChanged = netsnmp_get_agent_uptime();

        /*
         * remove entry from ifcontainer
         */
        CONTAINER_REMOVE(ipaddress_container, ipaddress_entry);
        netsnmp_access_ipaddress_entry_free(ipaddress_entry);
    }
}

/**
 * add new entry
 */
static void
_add_new_entry(netsnmp_ipaddress_entry * ipaddress_entry,
               netsnmp_container * container)
{
    ipAddressTable_rowreq_ctx *rowreq_ctx;

    DEBUGMSGTL(("ipAddressTable:access", "creating new entry\n"));

    netsnmp_assert(NULL != ipaddress_entry);
    netsnmp_assert(NULL != container);

    /*
     * allocate an row context and set the index(es)
     */
    rowreq_ctx = ipAddressTable_allocate_rowreq_ctx(ipaddress_entry);
    if ((NULL != rowreq_ctx) &&
        (MFD_SUCCESS ==
         ipAddressTable_indexes_set(rowreq_ctx,
                                    ipaddress_entry->ia_address_len,
                                    ipaddress_entry->ia_address,
                                    ipaddress_entry->ia_address_len))) {
        CONTAINER_INSERT(container, rowreq_ctx);
        rowreq_ctx->ipAddressLastChanged =
            rowreq_ctx->ipAddressCreated = netsnmp_get_agent_uptime();
    } else {
        if (NULL != rowreq_ctx) {
            snmp_log(LOG_ERR, "error setting index while loading "
                     "ipAddressTable cache.\n");
            ipAddressTable_release_rowreq_ctx(rowreq_ctx);
        } else {
            snmp_log(LOG_ERR, "memory allocation failed while loading "
                     "ipAddressTable cache.\n");
            netsnmp_access_ipaddress_entry_free(ipaddress_entry);
        }

        return;
    }

    /*-------------------------------------------------------------------
     * handle data that isn't part of the data_access ipaddress structure
     */
    rowreq_ctx->ipAddressRowStatus = ROWSTATUS_ACTIVE;
}

/**
 * load cache data
 *
 * TODO:350:M: Implement ipAddressTable cache load
 *
 * @param container container to which items should be inserted
 *
 * @retval MFD_SUCCESS              : success.
 * @retval MFD_RESOURCE_UNAVAILABLE : Can't access data source
 * @retval MFD_ERROR                : other error.
 *
 *  This function is called to cache the index(es) (and data, optionally)
 *  for the every row in the data set.
 *
 * @remark
 *  While loading the cache, the only important thing is the indexes.
 *  If access to your data is cheap/fast (e.g. you have a pointer to a
 *  structure in memory), it would make sense to update the data here.
 *  If, however, the accessing the data invovles more work (e.g. parsing
 *  some other existing data, or peforming calculations to derive the data),
 *  then you can limit yourself to setting the indexes and saving any
 *  information you will need later. Then use the saved information in
 *  ipAddressTable_row_prep() for populating data.
 *
 * @note
 *  If you need consistency between rows (like you want statistics
 *  for each row to be from the same time frame), you should set all
 *  data here.
 *
 */
int
ipAddressTable_cache_load(netsnmp_container * container)
{
    ipAddressTable_rowreq_ctx *rowreq_ctx;
    netsnmp_container *ipaddress_container;
    void           *tmp_ptr[2];

    DEBUGMSGTL(("verbose:ipAddressTable:ipAddressTable_cache_load",
                "called\n"));

    /*
     * TODO:351:M: |-> Load/update data in the ipAddressTable container.
     * loop over your ipAddressTable data, allocate a rowreq context,
     * set the index(es) [and data, optionally] and insert into
     * the container.
     */
    ipaddress_container =
        netsnmp_access_ipaddress_container_load(NULL,
                                                NETSNMP_ACCESS_IPADDRESS_INIT_ADDL_IDX_BY_ADDR);
    /*
     * we just got a fresh copy of interface data. compare it to
     * what we've already got, and make any adjustments, saving
     * missing addresses to be deleted.
     */
    tmp_ptr[0] = ipaddress_container->next;
    tmp_ptr[1] = NULL;
    CONTAINER_FOR_EACH(container, (netsnmp_container_obj_func *)
                       _check_entry_for_updates, tmp_ptr);

    /*
     * now add any new interfaces
     */
    CONTAINER_FOR_EACH(ipaddress_container,
                       (netsnmp_container_obj_func *) _add_new_entry,
                       container);

    /*
     * free the container. we've either claimed each entry, or released it,
     * so the access function doesn't need to clear the container.
     */
    netsnmp_access_ipaddress_container_free(ipaddress_container,
                                            NETSNMP_ACCESS_IPADDRESS_FREE_DONT_CLEAR);

    /*
     * remove deleted addresses from table container
     */
    if (NULL != tmp_ptr[1]) {
        netsnmp_container *tmp_container =
            (netsnmp_container *) tmp_ptr[1];
        ipAddressTable_rowreq_ctx *tmp_ctx;

        /*
         * this works because the tmp_container is a linked list,
         * which can be used like a stack...
         */
        while (CONTAINER_SIZE(tmp_container)) {
            /*
             * get from delete list
             */
            tmp_ctx = CONTAINER_FIRST(tmp_container);

            /*
             * release context, delete from table container
             */
            CONTAINER_REMOVE(container, tmp_ctx);
            ipAddressTable_release_rowreq_ctx(tmp_ctx);

            /*
             * pop off delete list
             */
            CONTAINER_REMOVE(tmp_container, NULL);
        }
    }

    DEBUGMSGT(("verbose:ipAddressTable:ipAddressTable_cache_load",
               "%d records\n", CONTAINER_SIZE(container)));

    return MFD_SUCCESS;
}                               /* ipAddressTable_cache_load */

/**
 * cache clean up
 *
 * @param container container with all current items
 *
 *  This optional callback is called prior to all
 *  item's being removed from the container. If you
 *  need to do any processing before that, do it here.
 *
 * @note
 *  The MFD helper will take care of releasing all the row contexts.
 *
 */
void
ipAddressTable_cache_free(netsnmp_container * container)
{
    DEBUGMSGTL(("verbose:ipAddressTable:ipAddressTable_cache_free",
                "called\n"));

    /*
     * TODO:380:M: Free ipAddressTable cache.
     */
}                               /* ipAddressTable_cache_free */

/**
 * prepare row for processing.
 *
 *  When the agent has located the row for a request, this function is
 *  called to prepare the row for processing. If you fully populated
 *  the data context during the index setup phase, you may not need to
 *  do anything.
 *
 * @param rowreq_ctx pointer to a context.
 *
 * @retval MFD_SUCCESS     : success.
 * @retval MFD_ERROR       : other error.
 */
int
ipAddressTable_row_prep(ipAddressTable_rowreq_ctx * rowreq_ctx)
{
    DEBUGMSGTL(("verbose:ipAddressTable:ipAddressTable_row_prep",
                "called\n"));

    netsnmp_assert(NULL != rowreq_ctx);

    /*
     * TODO:390:o: Prepare row for request.
     * If populating row data was delayed, this is the place to
     * fill in the row for this request.
     */

    return MFD_SUCCESS;
}                               /* ipAddressTable_row_prep */

/** @} */
