/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>

#include <mesos/mesos.hpp>
#include <mesos/resources.hpp>

#include <process/future.hpp>

#include <stout/duration.hpp>
#include <stout/flags.hpp>

#include <slave/flags.hpp>
#include <slave/containerizer/mesos/containerizer.hpp>

using std::cerr;
using std::cout;
using std::endl;
using std::string;

using namespace mesos;
using namespace process;


class LaunchFlags : public flags::FlagsBase
{
public:
  LaunchFlags()
  {
    add(&isolation,
        "isolation",
        "isolation",
        "posix/cpu,posix/mem");

    add(&container_id,
        "container_id",
        "ContainerID",
        "");

    add(&work_dir,
        "work_dir",
        "container work directory",
        "");

    add(&resources,
        "resources",
        "resources",
        "");

    add(&command,
        "command",
        "command",
        "");
  }

  string isolation;
  string container_id;
  string work_dir;
  string resources;
  string command;
};

int main(int argc, char* argv[])
{
  // look at argv[1] as the action to perform.

  if (argc < 2) {
    cerr << "oops!" << endl;
    return -1;
  }

  if (strcmp(argv[1], "destroy") == 0) {
    cerr << "ok" << endl;
    return 10;
  } else if (strcmp(argv[1], "launch") == 0) {
    LaunchFlags flags;

    Try<Nothing> load = flags.load(None(), &argc, &argv);
    if (load.isError()) {
      cerr << load.error() << endl;
      return -1;
    }

    Try<Resources> resources = Resources::parse(flags.resources, "*");
    if (resources.isError()) {
      cerr << resources.error() << endl;
      return -1;
    }

    mesos::internal::slave::Flags slaveFlags;
    slaveFlags.isolation = flags.isolation;
    slaveFlags.cgroups_root = "mesosd";

    Try<mesos::internal::slave::MesosContainerizer*> containerizer =
      mesos::internal::slave::MesosContainerizer::create(slaveFlags, true);

    if (containerizer.isError()) {
      cerr << containerizer.error() << endl;
      return -1;
    }

    ContainerID containerId;
    containerId.set_value(flags.container_id);

    ExecutorInfo executorInfo;
    executorInfo.mutable_executor_id()->set_value(flags.container_id);
    executorInfo.mutable_framework_id()->set_value(flags.container_id);
    executorInfo.mutable_resources()->CopyFrom(resources.get());

    CommandInfo commandInfo;
    commandInfo.set_value(flags.command);
    executorInfo.mutable_command()->CopyFrom(commandInfo);

    Future<bool> launch = containerizer.get()->launch(
        containerId,
        executorInfo,
        flags.work_dir,
        None(),
        SlaveID(),
        process::PID<mesos::internal::slave::Slave>(),
        false);


    launch.await(Seconds(10));
    if (!launch.isReady()) {
      cerr << "Failed to launch: "
           << (launch.isFailed() ? launch.failure() : "discarded")
           << endl;
      // XXX should destroy the container on any launch failures
      return -1;
    }

    Future<containerizer::Termination> wait =
      containerizer.get()->wait(containerId);
    wait.await();

    if (!wait.isReady()) {
      cerr << "failed to wait" << endl;
      return -1;
    }

    if (wait.get().killed()) {
      cerr << wait.get().message() << endl;
    }

    int status = wait.get().status();
    if (WIFEXITED(status)) {
      cerr << "Container exited with status: " << WEXITSTATUS(status) << endl;
      cerr << "Container exited with message: " << wait.get().message() << endl;

      return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      cerr << "Container terminated from signal: " << WTERMSIG(status) << endl;

      return -1;
    } else {
      return -1;
    }
  }

  // shouldn't get to here
  return -1;
}
