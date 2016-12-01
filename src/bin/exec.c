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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h> 
#include <string.h>

#include "config.h"
#include "lib/singularity.h"
#include "util/file.h"
#include "util/util.h"


int singularity_image_exec(char *containerimage, char **action_args) {

    singularity_action_init();
    singularity_rootfs_init(containerimage);
    singularity_sessiondir_init(containerimage);

    singularity_ns_unshare();

    singularity_rootfs_mount();

    singularity_rootfs_check();

    singularity_file();

    singularity_mount();

    singularity_rootfs_chroot();

    singularity_action_do(argc, action_args);
    
    return(0);
}
