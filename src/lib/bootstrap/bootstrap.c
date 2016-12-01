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


/*
 * Runs the specified script within the bootstrap spec file. Forks a child process and waits
 * until that process terminates to continue in the main process thread.
 *
 * @param char *section_name pointer to string containing section name of script to run
 * @returns nothing
 */
void bootstrap_script_run(char *section_name) {
    char **fork_args = malloc(sizeof(char *) * 4);
  
    singularity_message(VERBOSE, "Searching for %%%s bootstrap script\n", section_name);
    if ( singularity_bootdef_section_get(&fork_args[2], section_name) == -1 ) {
        singularity_message(VERBOSE, "No %%%s bootstrap script found, skipping\n", section_name);
        return;
    } else {
    
        fork_args[0] = strdup("/bin/sh");
        fork_args[1] = strdup("-c");
        fork_args[3] = NULL;
        singularity_message(VERBOSE, "Running %%%s bootstrap script\n%s%s\n%s\n", section_name, fork_args[0], fork_args[1], fork_args[2]);

        if ( singularity_fork_exec(fork_args) != 0 ) {
            singularity_message(WARNING, "Something may have gone wrong. %%%s script exited with non-zero status.\n", section_name);
        }
        free(fork_args[0]);
        free(fork_args[1]);
        free(fork_args[2]);
        free(fork_args[3]);
        free(fork_args);
    }
}

/*
 * Determines which module the bootstrap spec file belongs to and runs the appropriate workflow.
 *
 * @returns 0 on success, -1 on failure
 */
int bootstrap_module_init() {
    char *module_name;
    singularity_bootdef_rewind();

    if ( ( module_name = singularity_bootdef_get_value("BootStrap") ) == NULL ) {
        singularity_message(ERROR, "Bootstrap definition file does not contain required Bootstrap: option\n");
        return(-1);

    } else {
        singularity_message(VERBOSE, "Running bootstrap module %s\n", module_name);

/*
        if ( strcmp(module_name, "docker") == 0 ) { //Docker
            return( singularity_bootstrap_docker() );

        } else if ( strcmp(module_name, "yum") == 0 ) { //Yum
            return( singularity_bootstrap_yum() );

        } else if ( strcmp(module_name, "debootstrap") == 0 ) { //Debootstrap
            return( singularity_bootstrap_debootstrap() );

        } else if ( strcmp(module_name, "arch") == 0 ) { //Arch
            return( singularity_bootstrap_arch() );

        } else if ( strcmp(module_name, "busybox") == 0 ) { //Busybox
            return( singularity_bootstrap_busybox() );

        } else {
            singularity_message(ERROR, "Could not parse bootstrap module of type: %s\n", module_name);
            return(-1);
        }
*/
    }
    return(-1);
}

/*
 * Ensures that the paths are properly installed with correct permissions.
 *
 * @returns 0 on success, <0 on failure
 */
int bootstrap_rootfs_install() {
    int retval = 0;
    char *rootfs_path = singularity_rootfs_dir();
    retval += s_mkpath(rootfs_path, 0755);
    retval += s_mkpath(joinpath(rootfs_path, "/bin"), 0755);
    retval += s_mkpath(joinpath(rootfs_path, "/dev"), 0755);
    retval += s_mkpath(joinpath(rootfs_path, "/home"), 0755);
    retval += s_mkpath(joinpath(rootfs_path, "/etc"), 0755);
    retval += s_mkpath(joinpath(rootfs_path, "/root"), 0750);
    retval += s_mkpath(joinpath(rootfs_path, "/proc"), 0755);
    retval += s_mkpath(joinpath(rootfs_path, "/sys"), 0755);
    retval += s_mkpath(joinpath(rootfs_path, "/tmp"), 1777);
    retval += s_mkpath(joinpath(rootfs_path, "/var/tmp"), 1777);
    retval += copy_file("/etc/hosts", joinpath(rootfs_path, "/etc/hosts"));
    retval += copy_file("/etc/resolv.conf", joinpath(rootfs_path, "/etc/resolv.conf"));
    unlink(joinpath(rootfs_path, "/etc/mtab"));
    retval += fileput(joinpath(rootfs_path, "/etc/mtab"), "singularity / rootfs rw 0 0");
    
    free(rootfs_path);
    return(retval);

}

/*
 * Copies script given by section_name into file in container rootfs given
 * by dest_path.
 *
 * @param char *section_name pointer to string containing name of section to copy
 * @param char *dest_path pointer to string containing path to copy script into
 * @returns 0 if script was copied, -1 if script was not copied
 */
int bootstrap_copy_script(char *section_name, char *dest_path) {
    char **script = malloc(sizeof(char *));
    char *full_dest_path = joinpath(singularity_rootfs_dir(), dest_path);
    singularity_message(VERBOSE, "Attempting to copy %%%s script into %s in container.\n", section_name, dest_path);
    
    if ( singularity_bootdef_section_get(script, section_name) == -1 ) {
        singularity_message(VERBOSE, "Definition file does not contain %s, skipping.\n", section_name);
        free(full_dest_path);
        free(script);
        return(-1);
    }
    
    if ( fileput(full_dest_path, *script) < 0 ) {
        singularity_message(WARNING, "Couldn't write to %s, skipping %s.\n", full_dest_path, section_name);
        free(full_dest_path);
        free(*script);
        free(script);
        return(-1);
    }

    chmod(full_dest_path, 0755);

    free(full_dest_path);
    free(*script);
    free(script);
    return(0);
}
