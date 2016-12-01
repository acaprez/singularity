/* 
 * Copyright (c) 2016, Michael W. Bauer. All rights reserved.
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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>

#include "util/file.h"
#include "util/util.h"
#include "lib/message.h"
#include "lib/singularity.h"
#include "lib/action/test/test.h"
#include "bootstrap/bootdef_parser.h"
#include "bootstrap/bootstrap.h"


static char *module_name;
static char *rootfs_path;


int bootstrap(char *containerimage, char *bootdef_path) {
    char *driver_v1_path = LIBEXECDIR "/singularity/bootstrap/driver-v1.sh";
    singularity_message(VERBOSE, "Preparing to bootstrap image with definition file: %s\n", bootdef_path);

    /* Sanity check to ensure we can properly open the bootstrap definition file */
    singularity_message(DEBUG, "Opening singularity bootdef file: %s\n", bootdef_path);
    if( singularity_bootdef_open(bootdef_path) != 0 ) {
        singularity_message(ERROR, "Could not open bootstrap definition file\n");
        ABORT(255);
    }

    /* Initialize namespaces and session directory on the host */
    singularity_message(DEBUG, "Initializing container directory\n");
    singularity_sessiondir_init(containerimage);
    singularity_ns_user_unshare();
    singularity_ns_mnt_unshare();

    /* Initialize container rootfs directory and corresponding variables */
    singularity_message(DEBUG, "Mounting container rootfs\n");
    singularity_rootfs_init(containerimage);
    singularity_rootfs_mount();
    rootfs_path = singularity_rootfs_dir();
    
    /* Set environment variables required for any shell scripts we will call on */
    setenv("SINGULARITY_ROOTFS", rootfs_path, 1);
    setenv("SINGULARITY_IMAGE", containerimage, 1);
    setenv("SINGULARITY_BUILDDEF", bootdef_path, 1);

    /* Determine if Singularity file is v1 or v2. v1 files will directly use the old driver-v1.sh script */
    if( singularity_bootdef_get_version() == 1 ) {
        singularity_message(VERBOSE, "Running bootstrap driver v1\n");
        singularity_bootdef_close();
    
        /* Directly call on old driver-v1.sh */
        singularity_fork_exec(&driver_v1_path); //Use singularity_fork_exec to directly call the v1 driver
        return(0);

    } else {
        singularity_message(VERBOSE, "Running bootstrap driver v2\n");
    
        /* Run %pre script to replace prebootstrap module */    
        bootstrap_script_run("pre");

        /* Run appropriate module to create the base OS in the container */
        if ( bootstrap_module_init() != 0 ) {
            singularity_message(ERROR, "Something went wrong during build module. \n");
        }

        /* Run through postbootstrap module logic */
    
        /* Ensure that rootfs has required folders, permissions and files */
        singularity_rootfs_check();
    
        if ( bootstrap_rootfs_install() != 0 ) {
            singularity_message(ERROR, "Failed to create container rootfs. Aborting...\n");
            ABORT(255);
        }

        /* Copy runscript, environment, and .test files into container rootfs */
        bootstrap_copy_script("runscript", "/singularity");
        bootstrap_copy_script("test", "/.test");
        if ( bootstrap_copy_script("environment", "/environment") != 0 ) {
            singularity_message(VERBOSE, "Copying default environment file instead of user specified environment\n");
            copy_file(LIBEXECDIR "/singularity/defaults/environment", joinpath(rootfs_path, "/environment"));
        }
        chmod(joinpath(rootfs_path, "/environment"), 0644);
            

        /* Copy/mount necessary files directly into container rootfs */
        if ( singularity_file_bootstrap() < 0 ) {
            singularity_message(ERROR, "Failed to copy necessary default files to container rootfs. Aborting...\n");
            ABORT(255);
        }

        /* Mount necessary folders into container */
        if ( singularity_mount() < 0 ) {
            singularity_message(ERROR, "Failed to mount necessary files into container rootfs. Aborting...\n");
            ABORT(255);
        }

        /* Run %setup script from host */
        bootstrap_script_run("setup");

        /* Run %post and %test scripts from inside container */
        singularity_rootfs_chroot();
        bootstrap_script_run("post");
        action_test_do(0, NULL);
        
        singularity_bootdef_close();
    }
    return(0);
}
