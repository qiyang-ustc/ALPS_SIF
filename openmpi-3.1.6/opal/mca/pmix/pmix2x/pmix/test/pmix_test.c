/*
 * Copyright (c) 2004-2010 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2013 Los Alamos National Security, LLC.
 *                         All rights reserved.
 * Copyright (c) 2009-2012 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2011      Oak Ridge National Labs.  All rights reserved.
 * Copyright (c) 2013-2019 Intel, Inc.  All rights reserved.
 * Copyright (c) 2015      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * Copyright (c) 2015      Mellanox Technologies, Inc.
 *                         All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "src/util/pmix_environ.h"
#include "src/util/output.h"

#include "server_callbacks.h"
#include "utils.h"
#include "src/include/pmix_globals.h"

bool spawn_wait = false;

int main(int argc, char **argv)
{
    char **client_env=NULL;
    char **client_argv=NULL;
    int rc;
    struct stat stat_buf;
    struct timeval tv;
    double test_start;
    test_params params;
    INIT_TEST_PARAMS(params);
    int test_fail = 0;
    char *tmp;
    int ns_nprocs;

    gettimeofday(&tv, NULL);
    test_start = tv.tv_sec + 1E-6*tv.tv_usec;

    /* smoke test */
    if (PMIX_SUCCESS != 0) {
        TEST_ERROR(("ERROR IN COMPUTING CONSTANTS: PMIX_SUCCESS = %d", PMIX_SUCCESS));
        exit(1);
    }

    TEST_VERBOSE(("Testing version %s", PMIx_Get_version()));

    parse_cmd(argc, argv, &params);
    TEST_VERBOSE(("Start PMIx_lite smoke test (timeout is %d)", params.timeout));

    /* verify executable */
    if( 0 > ( rc = stat(params.binary, &stat_buf) ) ){
        TEST_ERROR(("Cannot stat() executable \"%s\": %d: %s", params.binary, errno, strerror(errno)));
        FREE_TEST_PARAMS(params);
        return 0;
    } else if( !S_ISREG(stat_buf.st_mode) ){
        TEST_ERROR(("Client executable \"%s\": is not a regular file", params.binary));
        FREE_TEST_PARAMS(params);
        return 0;
    }else if( !(stat_buf.st_mode & S_IXUSR) ){
        TEST_ERROR(("Client executable \"%s\": has no executable flag", params.binary));
        FREE_TEST_PARAMS(params);
        return 0;
    }

    /* setup the server library */
    pmix_info_t info[1];
    (void)strncpy(info[0].key, PMIX_SOCKET_MODE, PMIX_MAX_KEYLEN);
    info[0].value.type = PMIX_UINT32;
    info[0].value.data.uint32 = 0666;

    if (PMIX_SUCCESS != (rc = PMIx_server_init(&mymodule, info, 1))) {
        TEST_ERROR(("Init failed with error %d", rc));
        FREE_TEST_PARAMS(params);
        return rc;
    }
    /* register the errhandler */
    PMIx_Register_event_handler(NULL, 0, NULL, 0,
                                errhandler, errhandler_reg_callbk, NULL);

    cli_init(params.nprocs);

    /* set common argv and env */
    client_env = pmix_argv_copy(environ);
    set_client_argv(&params, &client_argv);

    tmp = pmix_argv_join(client_argv, ' ');
    TEST_VERBOSE(("Executing test: %s", tmp));
    free(tmp);

    int launched = 0;
    /* set namespaces and fork clients */
    if (NULL == params.ns_dist) {
        /* we have a single namespace for all clients */
        ns_nprocs = params.nprocs;
        rc = launch_clients(ns_nprocs, params.binary, &client_env, &client_argv);
        if (PMIX_SUCCESS != rc) {
            FREE_TEST_PARAMS(params);
            return rc;
        }
        launched += ns_nprocs;
    } else {
        char *pch;
        pch = strtok(params.ns_dist, ":");
        while (NULL != pch) {
            ns_nprocs = (int)strtol(pch, NULL, 10);
            if (params.nprocs < (uint32_t)(launched+ns_nprocs)) {
                TEST_ERROR(("Total number of processes doesn't correspond number specified by ns_dist parameter."));
                FREE_TEST_PARAMS(params);
                return PMIX_ERROR;
            }
            if (0 < ns_nprocs) {
                rc = launch_clients(ns_nprocs, params.binary, &client_env, &client_argv);
                if (PMIX_SUCCESS != rc) {
                    FREE_TEST_PARAMS(params);
                    return rc;
                }
            }
            pch = strtok (NULL, ":");
            launched += ns_nprocs;
        }
    }
    if (params.nprocs != (uint32_t)launched) {
        TEST_ERROR(("Total number of processes doesn't correspond number specified by ns_dist parameter."));
        cli_kill_all();
        test_fail = 1;
    }

    /* hang around until the client(s) finalize */
    while (!test_terminated()) {
        // To avoid test hang we want to interrupt the loop each 0.1s
        double test_current;

        // check if we exceed the max time
        gettimeofday(&tv, NULL);
        test_current = tv.tv_sec + 1E-6*tv.tv_usec;
        if( (test_current - test_start) > params.timeout ){
            break;
        }
        cli_wait_all(0);
    }

    if( !test_terminated() ){
        TEST_ERROR(("Test exited by a timeout!"));
        cli_kill_all();
        test_fail = 1;
    }

    if( test_abort ){
        TEST_ERROR(("Test was aborted!"));
        /* do not simply kill the clients as that generates
         * event notifications which these tests then print
         * out, flooding the log */
      //  cli_kill_all();
        test_fail = 1;
    }

    if (0 != params.test_spawn) {
        PMIX_WAIT_FOR_COMPLETION(spawn_wait);
    }

    pmix_argv_free(client_argv);
    pmix_argv_free(client_env);

    /* deregister the errhandler */
    PMIx_Deregister_event_handler(0, op_callbk, NULL);

    cli_wait_all(1.0);

    /* finalize the server library */
    if (PMIX_SUCCESS != (rc = PMIx_server_finalize())) {
        TEST_ERROR(("Finalize failed with error %d", rc));
    }

    FREE_TEST_PARAMS(params);

    if (0 == test_fail) {
        TEST_OUTPUT(("Test finished OK!"));
    }

    return test_fail;
}
