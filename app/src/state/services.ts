//
// RPiLEDConfig - app
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
// What the screens talk to: the phone's Bluetooth. The fake daemon
// (src/ble/fake.ts) implements the same interface for the tests only.
import { blePlxTransport } from '@/ble/blePlx';
import type { BleTransport } from '@/ble/transport';

export interface Services {
  transport: BleTransport;
}

export const services: Services = { transport: blePlxTransport };
