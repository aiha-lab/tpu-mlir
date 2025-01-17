#!/usr/bin/env python3
# ==============================================================================
#
# Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
#
# TPU-MLIR is licensed under the 2-Clause BSD License except for the
# third-party components.
#
# ==============================================================================

import sys
import logging


log_name = dict()
def setup_logger(name, log_level="INFO"):
    if name in log_name:
        return log_name[name]

    formatter = logging.Formatter(
        datefmt='%Y/%m/%d %H:%M:%S', fmt='%(asctime)s - %(levelname)s : %(message)s')

    handler = logging.StreamHandler(stream=sys.stderr)
    handler.setFormatter(formatter)

    logger = logging.getLogger(name)
    if log_level == "DEBUG":
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.INFO)
    logger.addHandler(handler)
    log_name[name] = logger
    return logger
