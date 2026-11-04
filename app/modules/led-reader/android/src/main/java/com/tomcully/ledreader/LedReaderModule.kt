// RPiLEDConfig - app. Copyright (C) 2026 T. Cully. Licensed under the Apache License, Version 2.0 - see LICENSE.
//
// Registers the "ledReader" frame-processor plugin with VisionCamera. `install()` is called
// from JS before the plugin is looked up, so the registration has happened whenever Expo
// creates this module.
package com.tomcully.ledreader

import com.mrousavy.camera.frameprocessors.FrameProcessorPluginRegistry
import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition

object LedReaderRegistration {
  private var registered = false

  @Synchronized
  fun register() {
    if (registered) return
    FrameProcessorPluginRegistry.addFrameProcessorPlugin("ledReader") { proxy, options -> LedReaderPlugin(proxy, options) }
    registered = true
  }
}

class LedReaderModule : Module() {
  override fun definition() = ModuleDefinition {
    Name("LedReader")

    OnCreate { LedReaderRegistration.register() }

    Function("install") {
      LedReaderRegistration.register()
      true
    }
  }
}
