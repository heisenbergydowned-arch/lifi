Import("env")
import os
root = os.path.abspath(os.path.join(env.subst("$PROJECT_DIR"), ".."))
env.BuildSources(os.path.join(env.subst("$BUILD_DIR"), "shared"),
                 os.path.join(root, "Software"),
                 src_filter=["-<*>", "+<IotProtocol.c>", "+<RtcCalendar.c>", "+<ProtocolConfig.c>"])
