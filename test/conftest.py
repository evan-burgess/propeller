# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2019 Red Hat, Inc.
# Copyright (C) 2022 Seagate Technology LLC and/or its Affiliates.
# Derived from the sanlock file of the same name.

import logging
import os

import pytest

from . import ilm_util

import idm_api
from test_conf import *     # Normally bad practice, but only importing 'constants' here

_logger = logging.getLogger(__name__)

RAW_DEVICES = [SG_DEVICE1,
               SG_DEVICE2,
               SG_DEVICE3,
               SG_DEVICE4,
               SG_DEVICE5,
               SG_DEVICE6,
               SG_DEVICE7,
               SG_DEVICE8]

@pytest.fixture(scope="session")
def ilm_daemon():
    _logger.info('ilm_daemon start')

    _init_devices()

    """
    Run ILM daemon during a test.
    """
    p = ilm_util.start_daemon()
    try:
        ilm_util.wait_for_daemon(0.5)
        yield
    finally:
        # Killing lock manager allows terminating without removing the lockspace.
        p.kill()
        p.wait()

@pytest.fixture(scope="session")
def idm_cleanup():
    _logger.info('idm_cleanup start')

    _init_devices()

def _init_devices():
    LOCK_ID0 ='0000000000000000000000000000000000000000000000000000000000000000'
    SG_CMD   = 'sg_raw -v -s 512 -i zero.bin'
    SG_DATA  = 'F0 00 00 00 00 00 00 00 00 00 00 00 02 00 FF 00'

    NVME_CMD = 'nvme admin-passthru'
    NVME_DATA = '--opcode=0xC1 --data-len=512 --write --cdw12=0x0000FF00 --cdw10=0x00000080 --input-file=zero.bin'

    os.system('dd if=/dev/zero of=zero.bin count=1 bs=512')

    for device in RAW_DEVICES:
        try:
            if device.find('nvme') >= 0:
                _logger.info(f'possible nvme device found: {device}')
                os.system(f'{NVME_CMD} {device} {NVME_DATA}')
            else:
                _logger.info(f'possible scsi device found: {device}')
                os.system(f'{SG_CMD} {device} {SG_DATA}')
        except Exception as e:
            _logger.error(f'{device} reset failure')
            raise e from None

    for device in RAW_DEVICES:
        try:
            idm_api.idm_drive_lock_mode(LOCK_ID0, device)
        except Exception as e:
            _logger.error(f'{device} lock failure')
            raise e from None

def pytest_addoption(parser):
    parser.addoption('--run-destroy', action='store_true', dest="destroy",
                     default=False, help="enable destroy test")

def pytest_configure(config):
    if not config.option.destroy:
        setattr(config.option, 'markexpr', 'not destroy')



@pytest.fixture(scope="module")
def nvme_async_threads():
    try:
        idm_api.idm_manual_startup()
    except Exception as e:
        _logger.error(f'idm_manual_startup failure:{e}')
        raise e from None

    yield

    try:
        idm_api.idm_manual_shutdown()
    except Exception as e:
        _logger.error(f'idm_manual_shutdown failure:{e}')
        raise e from None

