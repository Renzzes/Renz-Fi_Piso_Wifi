Import("env")

framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
env.Append(CPPPATH=[
    framework_dir + "/libraries/Network/src",
    framework_dir + "/libraries/FS/src",
    framework_dir + "/libraries/Hash/src",
])
