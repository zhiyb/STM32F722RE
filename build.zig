const std = @import("std");

pub fn build_bin_to_h(b: *std.Build) *std.Build.Step.Compile {
    const exe = b.addExecutable(.{
        .name = "bin_to_h",
        .root_module = b.createModule(.{
            .root_source_file = b.path("tools/bin_to_h.zig"),
            .target = b.graph.host,
        }),
    });
    return exe;
}

// Although this function looks imperative, it does not perform the build
// directly and instead it mutates the build graph (`b`) that will be then
// executed by an external runner. The functions in `std.Build` implement a DSL
// for defining build steps and express dependencies between them, allowing the
// build runner to parallelize the build automatically (and the cache system to
// know when a step doesn't need to be re-run).
pub fn build(b: *std.Build) void {
    // Standard target options allow the person running `zig build` to choose
    // what target to build for. Here we do not override the defaults, which
    // means any target is allowed, and the default is native. Other options
    // for restricting supported target set are available.
    // const target = b.standardTargetOptions(.{});
    var features = std.Target.Cpu.Feature.Set.empty;
    features.addFeature(@intFromEnum(std.Target.arm.Feature.fp_armv8d16sp));

    const target = b.resolveTargetQuery(.{
        .cpu_arch = .thumb,
        .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_m7 },
        .cpu_features_add = features,
        .abi = .musleabihf,
        .os_tag = .freestanding,
    });

    const target_c_sources = .{"src/startup_stm32f722xx.s"};
    const target_flags = .{"-DSTM32F722xx"};

    // Standard optimization options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall. Here we do not
    // set a preferred release mode, allowing the user to decide how to optimize.
    const optimize = b.standardOptimizeOption(.{});
    // It's also possible to define more custom flags to toggle optional features
    // of this build script using `b.option()`. All defined flags (including
    // target and optimize options) will be listed when running `zig build --help`
    // in this directory.

    // // This creates a module, which represents a collection of source files alongside
    // // some compilation options, such as optimization mode and linked system libraries.
    // // Zig modules are the preferred way of making Zig code available to consumers.
    // // addModule defines a module that we intend to make available for importing
    // // to our consumers. We must give it a name because a Zig package can expose
    // // multiple modules and consumers will need to be able to specify which
    // // module they want to access.
    // const mod = b.addModule("zig", .{
    //     // The root source file is the "entry point" of this module. Users of
    //     // this module will only be able to access public declarations contained
    //     // in this file, which means that if you have declarations that you
    //     // intend to expose to consumers that were defined in other files part
    //     // of this module, you will have to make sure to re-export them from
    //     // the root file.
    //     .root_source_file = b.path("src/root.zig"),
    //     // Later on we'll use this module as the root module of a test executable
    //     // which requires us to specify a target.
    //     .target = target,
    // });

    // Here we define an executable. An executable needs to have a root module
    // which needs to expose a `main` function. While we could add a main function
    // to the module defined above, it's sometimes preferable to split business
    // logic and the CLI into two separate modules.
    //
    // If your goal is to create a Zig library for others to use, consider if
    // it might benefit from also exposing a CLI tool. A parser library for a
    // data serialization format could also bundle a CLI syntax checker, for example.
    //
    // If instead your goal is to create an executable, consider if users might
    // be interested in also being able to embed the core functionality of your
    // program in their own executable in order to avoid the overhead involved in
    // subprocessing your CLI tool.
    //
    // If neither case applies to you, feel free to delete the declaration you
    // don't need and to put everything under a single module.
    const fw_exe = b.addExecutable(.{
        .name = "fw",
        .root_module = b.createModule(.{
            // b.createModule defines a new module just like b.addModule but,
            // unlike b.addModule, it does not expose the module to consumers of
            // this package, which is why in this case we don't have to give it a name.
            // .root_source_file = b.path("src/main.zig"),
            // Target and optimization levels must be explicitly wired in when
            // defining an executable or library (in the root module), and you
            // can also hardcode a specific target for an executable or library
            // definition if desireable (e.g. firmware for embedded devices).
            .target = target,
            .optimize = optimize,
            // List of modules available for import in source files part of the
            // root module.
            // .imports = &.{
            //     // Here "zig" is the name you will use in your source code to
            //     // import this module (e.g. `@import("zig")`). The name is
            //     // repeated because you are allowed to rename your imports, which
            //     // can be extremely useful in case of collisions (which can happen
            //     // importing modules from different packages).
            //     .{ .name = "zig", .module = mod },
            // },
        }),
    });

    // C source files
    fw_exe.addCSourceFiles(.{
        .files = &(target_c_sources ++ .{
            "src/irq.c",
            "src/main.c",
            "src/panic.c",
            "src/log.c",
            "src/systick.c",
            "src/flash.c",
            "src/usb.c",
            "src/usb_hw.c",
            "src/usb_hw_ep.c",
            "src/usb_ep0_setup.c",
            "src/usb_desc.c",
            // "src/dma.c",
            // "src/uart.c",
            // "src/usb_hid.c",
            // "src/usb_cdc.c",
        }),
        .flags = &(target_flags ++ .{}),
    });

    fw_exe.setLinkerScript(b.path("STM32F722RE_FW_ITCM.ld"));

    // USB bootloader
    const bl_usb_exe = b.addExecutable(.{
        .name = "bl_usb",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
    });

    bl_usb_exe.addCSourceFiles(.{
        .files = &(target_c_sources ++ .{
            "src/irq.c",
            "src/boot_usb.c",
            "src/panic.c",
            "src/log.c",
            "src/systick.c",
            "src/flash.c",
            "src/usb.c",
            "src/usb_hw.c",
            "src/usb_hw_ep.c",
            "src/usb_ep0_setup.c",
            "src/usb_desc.c",
            "src/usb_dfu.c",
        }),
        .flags = &(target_flags ++ .{"-DBOOTLOADER"}),
    });

    bl_usb_exe.setLinkerScript(b.path("STM32F722RE_BL_USB.ld"));

    // Flash bootloader
    const bl_flash_exe = b.addExecutable(.{
        .name = "bl_flash",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
    });

    bl_flash_exe.addCSourceFiles(.{
        .files = &.{
            "src/irq.c",
            "src/boot_flash.c",
        },
        .flags = &(target_flags ++ .{"-DBOOTLOADER_FLASH"}),
    });

    bl_flash_exe.setLinkerScript(b.path("STM32F722RE_BL_FLASH.ld"));

    inline for (.{ bl_usb_exe, bl_flash_exe, fw_exe }) |exe| {
        // C include paths
        inline for (.{
            "cmsis",
            "hal",
            "inc",
        }) |inc| {
            exe.addIncludePath(b.path(inc));
        }

        exe.entry = .{ .symbol_name = "Reset_Handler" };

        // Use atomic symbols from compiler_rt
        // exe.bundle_compiler_rt = true;
        // exe.linkSystemLibrary("atomic");

        // Keep debug and frame pointers for debugging
        exe.root_module.strip = false;
        exe.root_module.omit_frame_pointer = false;
        // exe.link_data_sections = true;
        // exe.link_function_sections = true;
        exe.link_gc_sections = true;
        // LTO seems to cause compiler bugs
        // exe.want_lto = true;

        // This declares intent for the executable to be installed into the
        // install prefix when running `zig build` (i.e. when executing the default
        // step). By default the install prefix is `zig-out/` but can be overridden
        // by passing `--prefix` or `-p`.
        // b.installArtifact(exe);
        const installElf = b.addInstallBinFile(exe.getEmittedBin(), b.fmt("{s}.elf", .{exe.out_filename}));
        b.getInstallStep().dependOn(&installElf.step);

        const bin = exe.addObjCopy(.{ .format = .bin });
        const installBin = b.addInstallBinFile(bin.getOutput(), b.fmt("{s}.bin", .{exe.out_filename}));
        b.getInstallStep().dependOn(&installBin.step);
    }

    const bl_usb_bin = bl_usb_exe.addObjCopy(.{ .format = .bin });
    const bl_usb_h_tool = b.addRunArtifact(build_bin_to_h(b));
    bl_usb_h_tool.addFileArg(bl_usb_bin.getOutput());
    const bl_usb_h = bl_usb_h_tool.addOutputFileArg("bl_usb.h");
    bl_flash_exe.root_module.addIncludePath(bl_usb_h.dirname());

    // // This creates a top level step. Top level steps have a name and can be
    // // invoked by name when running `zig build` (e.g. `zig build run`).
    // // This will evaluate the `run` step rather than the default step.
    // // For a top level step to actually do something, it must depend on other
    // // steps (e.g. a Run step, as we will see in a moment).
    // const run_step = b.step("run", "Run the app");

    // // This creates a RunArtifact step in the build graph. A RunArtifact step
    // // invokes an executable compiled by Zig. Steps will only be executed by the
    // // runner if invoked directly by the user (in the case of top level steps)
    // // or if another step depends on it, so it's up to you to define when and
    // // how this Run step will be executed. In our case we want to run it when
    // // the user runs `zig build run`, so we create a dependency link.
    // const run_cmd = b.addRunArtifact(fw_exe);
    // run_step.dependOn(&run_cmd.step);

    // // By making the run step depend on the default step, it will be run from the
    // // installation directory rather than directly from within the cache directory.
    // run_cmd.step.dependOn(b.getInstallStep());

    // // This allows the user to pass arguments to the application in the build
    // // command itself, like this: `zig build run -- arg1 arg2 etc`
    // if (b.args) |args| {
    //     run_cmd.addArgs(args);
    // }

    // // Creates an executable that will run `test` blocks from the provided module.
    // // Here `mod` needs to define a target, which is why earlier we made sure to
    // // set the releative field.
    // const mod_tests = b.addTest(.{
    //     .root_module = mod,
    // });

    // // A run step that will run the test executable.
    // const run_mod_tests = b.addRunArtifact(mod_tests);

    // // Creates an executable that will run `test` blocks from the executable's
    // // root module. Note that test executables only test one module at a time,
    // // hence why we have to create two separate ones.
    // const exe_tests = b.addTest(.{
    //     .root_module = exe.root_module,
    // });

    // // A run step that will run the second test executable.
    // const run_exe_tests = b.addRunArtifact(exe_tests);

    // // A top level step for running all tests. dependOn can be called multiple
    // // times and since the two run steps do not depend on one another, this will
    // // make the two of them run in parallel.
    // const test_step = b.step("test", "Run tests");
    // test_step.dependOn(&run_mod_tests.step);
    // test_step.dependOn(&run_exe_tests.step);

    // Just like flags, top level steps are also listed in the `--help` menu.
    //
    // The Zig build system is entirely implemented in userland, which means
    // that it cannot hook into private compiler APIs. All compilation work
    // orchestrated by the build system will result in other Zig compiler
    // subcommands being invoked with the right flags defined. You can observe
    // these invocations when one fails (or you pass a flag to increase
    // verbosity) to validate assumptions and diagnose problems.
    //
    // Lastly, the Zig build system is relatively simple and self-contained,
    // and reading its source code will allow you to master it.
}
