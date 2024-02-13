#!/usr/bin/env python3

#
# Copyright 2023 Two Six Technologies
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""
Script to build libzip for RACE
"""

import logging
import os
import race_ext_builder as builder


def get_cli_arguments():
    """Parse command-line arguments to the script"""
    parser = builder.get_arg_parser("jel2", "1.0.0", 1, __file__)
    return builder.normalize_args(parser.parse_args())


def build_android(args, env):
    env["JPEGDIR"]="/build/cache/jel2/1.0.0-1/android-arm64-v8a/source/jel2/jpeg-6b"
    
    # Compile dependencies
    target = "x86_64-linux-android" if "x86" in args.target else "aarch64-linux-android"
    builder.execute(
        args,
        [
            "./configure",
            f"--host=aarch64-linux-android",
            f"--target={target}",
            f"--prefix=/android/arm64-v8a",
        ],
        cwd=f"{args.source_dir}/jel2/jpeg-6b",
        env=env
    )
    
    # sed -i "s/HAVE_STDLIB_H/HAVE_STDLIB_H 1/g" jconfig.h
    builder.execute(
        args,
        [
            "sed",
            "-i",
            "s/HAVE_STDLIB_H/HAVE_STDLIB_H 1/g",
            "jconfig.h"
        ],
        cwd=f"{args.source_dir}/jel2/jpeg-6b",
        env=env
    )
    # sed -i "s/HAVE_STDDEF_H/HAVE_STDDEF_H 1/g" jconfig.h
    builder.execute(
        args,
        [
            "sed",
            "-i",
            "s/HAVE_STDDEF_H/HAVE_STDDEF_H 1/g",
            "jconfig.h"
        ],
        cwd=f"{args.source_dir}/jel2/jpeg-6b",
        env=env
    )
    
    builder.execute(
        args,
        ["make"],
        cwd=f"{args.source_dir}/jel2/jpeg-6b",
        env=env
    )
    
    # Configure and build jel2 proper
    builder.execute(
        args,
        [
            "autoreconf", "-i"
        ],
        cwd=f"{args.source_dir}/jel2",
        env=env
    )
    env["CFLAGS"] = "-Ijpeg-6b"
    env["LDFLAGS"] = "-Ljpeg-6b"
    env["LIBS"] = "-ljpeg"
    builder.execute(
        args,
        [
            "./configure",
            f"--prefix=/android/arm64-v8a",
            f"--host=x86_64",
            f"--target=aarch64-linux-android"
        ],
        cwd=f"{args.source_dir}/jel2",
        env=env
    )
    builder.execute(
        args,
        ["make"],
        cwd=f"{args.source_dir}/jel2",
        env=env
    )


def build_linux(args, env):
    builder.execute(
        args,
        [
            "autoreconf", "-fvi"
        ],
        cwd=f"{args.source_dir}/jel2",
        env=env
    )
    builder.execute(
        args,
        [
            "./configure",
            f"--prefix=",
            f"--host=x86_64"
        ],
        cwd=f"{args.source_dir}/jel2",
        env=env
    )
    

if __name__ == "__main__":
    args = get_cli_arguments()
    print(f"ARGS: {args}")

    builder.make_dirs(args)
    builder.setup_logger(args)

    builder.install_packages(
        args,
        [
            ("autoconf", None, False),
            ("libjpeg-turbo8-dev", None, True),
        ],
    )

    source_dir = os.path.join(args.source_dir, f"jel2")
    env = builder.create_standard_envvars(args)
    # env["CPPFLAGS"] = env.get("CPPFLAGS", "") + " -I/usr/include/"
    logging.root.info("Configuring build")
    logging.root.info(f"{args}")
    logging.root.info(f"{env}")
    builder.copy(args, f"{args.code_dir}/jel2", args.source_dir)
    if "android" in args.target:
        build_android(args, env)
    else:
        build_linux(args, env)
    
    logging.root.info("Building")
    builder.execute(args, ["make",], cwd=f"{args.source_dir}/jel2", env=env)
    builder.execute(args, ["make", "install"], cwd=f"{args.source_dir}/jel2", env=env)
    builder.create_package(args)
