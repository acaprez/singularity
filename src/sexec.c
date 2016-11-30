/* 
 * Copyright (c) 2015-2016, Gregory M. Kurtzer. All rights reserved.
 * 
 * “Singularity” Copyright (c) 2016, The Regents of the University of California,
 * through Lawrence Berkeley National Laboratory (subject to receipt of any
 * required approvals from the U.S. Dept. of Energy).  All rights reserved.
 * 
 * This software is licensed under a customized 3-clause BSD license.  Please
 * consult LICENSE file distributed with the sources of this project regarding
 * your rights to use or distribute this software.
 * 
 * NOTICE.  This Software was developed under funding from the U.S. Department of
 * Energy and the U.S. Government consequently retains certain rights. As such,
 * the U.S. Government has been granted for itself and others acting on its
 * behalf a paid-up, nonexclusive, irrevocable, worldwide license in the Software
 * to reproduce, distribute copies to the public, prepare derivative works, and
 * perform publicly and display publicly, and to permit other to do so. 
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

#include "config.h"
#include "lib/singularity.h"
#include "util/util.h"
#include "util/file.h"

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif

int main(int argc, char **argv) {
    char *image;
    char *command;

    // Before we do anything, check privileges and drop permission
    singularity_priv_init();
    singularity_priv_drop();

#ifdef SINGULARITY_SUID
    singularity_message(VERBOSE2, "Running SUID program workflow\n");

    singularity_message(VERBOSE2, "Checking program has appropriate permissions\n");
    if ( ( getuid() != 0 ) && ( ( is_owner("/proc/self/exe", 0) < 0 ) || ( is_suid("/proc/self/exe") < 0 ) ) ) {
        singularity_abort(255, "This program must be SUID root\n");
    }

    singularity_message(VERBOSE2, "Checking configuration file is properly owned by root\n");
    if ( is_owner(joinpath(SYSCONFDIR, "/singularity/singularity.conf"), 0 ) < 0 ) {
        singularity_abort(255, "Running in privileged mode, root must own the Singularity configuration file\n");
    }

    singularity_config_open(joinpath(SYSCONFDIR, "/singularity/singularity.conf"));

    singularity_config_rewind();
    
    singularity_message(VERBOSE2, "Checking that we are allowed to run as SUID\n");
    if ( singularity_config_get_bool("allow setuid", 1) == 0 ) {
        singularity_abort(255, "SUID mode has been disabled by the sysadmin... Aborting\n");
    }

    singularity_message(VERBOSE2, "Checking if we were requested to run as NOSUID by user\n");
    if ( envar_defined("SINGULARITY_NOSUID") == TRUE ) {
        singularity_abort(1, "NOSUID mode has been requested... Aborting\n");
    }

#else
    singularity_message(VERBOSE, "Running NON-SUID program workflow\n");

    singularity_message(DEBUG, "Checking program has appropriate permissions\n");
    if ( is_suid("/proc/self/exe") >= 0 ) {
        singularity_abort(255, "This program must **NOT** be SUID\n");
    }

    singularity_config_open(joinpath(SYSCONFDIR, "/singularity/singularity.conf"));

    singularity_config_rewind();

    if ( singularity_priv_getuid() != 0 ) {
        singularity_message(VERBOSE2, "Checking that we are allowed to run as SUID\n");
        if ( singularity_config_get_bool("allow setuid", 1) == 1 ) {
            singularity_message(VERBOSE2, "Checking if we were requested to run as NOSUID by user\n");
            if ( envar_defined("SINGULARITY_NOSUID") == FALSE ) {
                char sexec_suid_path[] = LIBEXECDIR "/singularity/sexec-suid";
		
		singularity_message(VERBOSE, "Checking for sexec-suid at %s\n", sexec_suid_path);

		if ( is_file(sexec_suid_path) == 0 ) {
                    if ( ( is_owner(sexec_suid_path, 0 ) == 0 ) && ( is_suid(sexec_suid_path) == 0 ) ) {
                        singularity_message(VERBOSE, "Invoking SUID sexec: %s\n", sexec_suid_path);

                        execv(sexec_suid_path, argv); // Flawfinder: ignore
                        singularity_abort(255, "Failed to execute sexec binary (%s): %s\n", sexec_suid_path, strerror(errno));
                    } else {
                        singularity_message(VERBOSE, "Not invoking SUID mode: SUID sexec permissions not properly set\n");
                    }
		}
		else {
		    singularity_message(VERBOSE, "Not invoking SUID mode: SUID sexec not installed\n");
		}
            } else {
                singularity_message(VERBOSE, "Not invoking SUID mode: NOSUID mode requested\n");
            }
        } else {
            singularity_message(VERBOSE, "Not invoking SUID mode: disallowed by the system administrator\n");
        }
    } else {
        singularity_message(VERBOSE, "Not invoking SUID mode: running as root\n");
    }

#endif /* SINGULARITY_SUID */


    singularity_message(DEBUG, "Entering logic portion of code\n");

    if ( ( command = envar("SINGULARITY_COMMAND", "", 64) ) != NULL ) {
        singularity_message(DEBUG, "Checking SINGULARITY_COMMAND value: %s\n", command);
        if ( ( strcmp(command, "shell") == 0 ) || ( strcmp(command, "exec") == 0 ) || ( strcmp(command, "run") == 0 ) || ( strcmp(command, "test") == 0 ) ) {
            singularity_message(VERBOSE, "Running container workflow\n");
            if ( ( image = envar_path("SINGULARITY_IMAGE") ) == NULL ) {
                singularity_abort(255, "SINGULARITY_IMAGE not defined!\n");
            }

            singularity_action_init();
            singularity_rootfs_init(image);
            singularity_sessiondir_init(image);

            free(image);

            singularity_ns_unshare();
            singularity_rootfs_mount();
            singularity_rootfs_check();
            singularity_file();
            singularity_mount();
            singularity_rootfs_chroot();
            singularity_action_do(argc, argv); // This will exec, so no need to return()
        }
    } else {
        // TODO: Move this all to SINGULARITY_COMMAND section
        if ( argv[1] == NULL ) {
            fprintf(stderr, "USAGE: %s [bootstrap/mount/bind/create/expand] [args]\n", argv[0]);
            return(1);
        }

        if ( strcmp(argv[1], "create") == 0 ) {
            long int size = 768;
            char *loop_dev;
            FILE *image_fp;

            if ( argv[2] == NULL ) {
                fprintf(stderr, "USAGE: %s create [singularity container image] [size in MiB]\n", argv[0]);
                exit(1);
            }
            if ( argv[3] != NULL ) {
                size = ( strtol(argv[3], (char **)NULL, 10) );
                singularity_message(DEBUG, "Setting size to: %d\n", size);
            }

            image = strdup(argv[2]);

            if ( singularity_image_create(image, size) < 0 ) {
                singularity_abort(255, "Failed creating image.\n");
            }

            singularity_sessiondir_init(image);
            singularity_ns_unshare();

            if ( ( image_fp = fopen(image, "r+") ) == NULL ) { // Flawfinder: ignore
                singularity_message(ERROR, "Could not open image (read write) %s: %s\n", image, strerror(errno));
                ABORT(255);
            }

            if ( ( loop_dev = singularity_loop_bind(image_fp) ) == NULL ) {
                singularity_abort(255, "Could not bind to loop device\n");
            }

            free(image);

            singularity_priv_escalate();
            singularity_message(INFO, "Formatting image with filesystem\n");
            if ( execlp("mkfs.ext3", "mkfs.ext3", "-q", loop_dev, NULL) < 0 ) {
                singularity_abort(255, "Failed exec'ing mkfs.ext3 %s\n", strerror(errno));
            }

        } else if ( strcmp(argv[1], "bootstrap") == 0 ) {
            char helper[] = LIBEXECDIR "/singularity/bootstrap/main.sh";
            char *bootstrap_def;
            char *rootfs_dir;
            if ( argv[2] == NULL ) {
                fprintf(stderr, "USAGE: %s bootstrap [singularity container image] [bootstrap definition]\n", argv[0]);
                exit(1);
            } else {
                image = strdup(argv[2]);
            }

            if ( argv[3] == NULL ) {
                bootstrap_def = strdup("");
            } else {
                bootstrap_def = strdup(argv[3]);
            }

            singularity_rootfs_init(image);
            singularity_sessiondir_init(image);

            free(image);

            singularity_ns_unshare();
            singularity_rootfs_mount();

            if ( ( rootfs_dir = singularity_rootfs_dir() ) < 0 ) {
                singularity_abort(255, "Could not identify the rootfs_dir\n");
            }

            setenv("SINGULARITY_ROOTFS", rootfs_dir, 1);

            if ( execlp(helper, helper, bootstrap_def, NULL) < 0  ) {
                singularity_abort(255, "Failed exec'ing mkfs.ext3 %s\n", strerror(errno));
            }

        } else {
            singularity_message(WARNING, "No idea what to do... Byes.\n");
        }
    }

    return(0);

}
