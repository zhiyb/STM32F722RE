const std = @import("std");

// Although this function looks imperative, note that its job is to
// declaratively construct a build graph that will be executed by an external
// runner.
pub fn build(b: *std.Build) void {
    // Run `zig build update-svd` to update HAL from SVD
    // const hal = b.addSystemCommand(&.{"python3"});
    // hal.addFileArg(b.path("svd2zig.py"));
    // hal.addFileArg(b.path("STM32F103.svd"));
    // const hal_out = hal.addOutputFileArg("stm32f103_svd.zig");

    // const wf = b.addUpdateSourceFiles();
    // wf.addCopyFileToSource(hal_out, "src/stm32f103_svd.zig");

    // const update_hal = b.step("update-svd", "Update Zig HAL code from SVD");
    // update_hal.dependOn(&wf.step);

    // Standard target options allows the person running `zig build` to choose
    // what target to build for. Here we do not override the defaults, which
    // means any target is allowed, and the default is native. Other options
    // for restricting supported target set are available.
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .thumb,
        .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_m0plus },
        .abi = .eabi,
        .os_tag = .freestanding,
    });

    // Standard optimization options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall. Here we do not
    // set a preferred release mode, allowing the user to decide how to optimize.
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "fw",
        // .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });

    // C source files
    exe.addCSourceFiles(.{ .files = &.{
        "src/main.c",
        "src/startup_stm32c071xx.s",
        "src/systick.c",
        "src/usb_hw.c",
        "src/usb_hw_ep.c",
        "src/usb_ep0_setup.c",
        "src/usb_desc.c",
        "src/usb_hid.c",
    }, .flags = &.{
        "-DSTM32C071xx",
    } });

    // C include paths
    exe.addIncludePath(b.path("cmsis"));
    exe.addIncludePath(b.path("inc"));

    // Use atomic symbols from compiler_rt
    // exe.bundle_compiler_rt = true;
    // exe.linkSystemLibrary("atomic");

    // Keep debug and frame pointers for debugging
    exe.root_module.strip = false;
    exe.root_module.omit_frame_pointer = false;
    // LTO seems to cause compiler bugs
    // exe.want_lto = true;

    exe.setLinkerScript(b.path("flash.ld"));
    exe.entry = .{ .symbol_name = "Reset_Handler" };

    // This declares intent for the executable to be installed into the
    // standard location when the user invokes the "install" step (the default
    // step when running `zig build`).
    b.installArtifact(exe);

    // This *creates* a Run step in the build graph, to be executed when another
    // step is evaluated that depends on it. The next line below will establish
    // such a dependency.
    const run_cmd = b.addRunArtifact(exe);

    // By making the run step depend on the install step, it will be run from the
    // installation directory rather than directly from within the cache directory.
    // This is not necessary, however, if the application depends on other installed
    // files, this ensures they will be present and in the expected location.
    run_cmd.step.dependOn(b.getInstallStep());

    // This allows the user to pass arguments to the application in the build
    // command itself, like this: `zig build run -- arg1 arg2 etc`
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    // This creates a build step. It will be visible in the `zig build --help` menu,
    // and can be selected like this: `zig build run`
    // This will evaluate the `run` step rather than the default, which is "install".
    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);

    // Creates a step for unit testing. This only builds the test executable
    // but does not run it.
    // const exe_unit_tests = b.addTest(.{
    //     .root_source_file = b.path("src/main.zig"),
    //     .target = b.standardTargetOptions(.{}),
    //     .optimize = optimize,
    // });

    // const run_exe_unit_tests = b.addRunArtifact(exe_unit_tests);

    // Similar to creating the run step earlier, this exposes a `test` step to
    // the `zig build --help` menu, providing a way for the user to request
    // running the unit tests.
    // const test_step = b.step("test", "Run unit tests");
    // test_step.dependOn(&run_exe_unit_tests.step);
}
