const std = @import("std");

pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();

    const alloc = arena.allocator();
    const args = try std.process.argsAlloc(alloc);
    if (args.len != 3)
        fatal("wrong number of arguments: {any}", .{args.len});

    const input_file_path = args[1];
    const output_file_path = args[2];

    var input_file = std.fs.cwd().openFile(input_file_path, .{ .mode = .read_only }) catch |err|
        fatal("unable to open '{s}': {s}", .{ input_file_path, @errorName(err) });
    defer input_file.close();

    var output_file = std.fs.cwd().createFile(output_file_path, .{}) catch |err|
        fatal("unable to open '{s}': {s}", .{ output_file_path, @errorName(err) });
    defer output_file.close();

    const input_stat = try input_file.stat();
    var input_buffer: [4096]u8 = undefined;
    var input_reader = input_file.reader(&input_buffer);
    const input_data = try input_reader.interface.readAlloc(alloc, input_stat.size);

    var output_buffer: [4096]u8 = undefined;
    var output_writer = output_file.writer(&output_buffer);

    var ofs: usize = 0;
    while (ofs < input_data.len) {
        var str: std.ArrayList(u8) = .empty;
        defer str.deinit(alloc);
        for (0..16) |i| {
            if (ofs + i >= input_data.len)
                break;
            var buf: [32]u8 = undefined;
            const seg = try std.fmt.bufPrint(&buf, "0x{x:0>2}, ", .{input_data[ofs + i]});
            try str.appendSlice(alloc, seg);
        }
        try str.appendSlice(alloc, "\n");
        _ = try output_writer.interface.write(str.items);
        ofs += 16;
    }

    try output_writer.interface.flush();
    return std.process.cleanExit();
}

fn fatal(comptime format: []const u8, args: anytype) noreturn {
    std.debug.print(format, args);
    std.process.exit(1);
}
