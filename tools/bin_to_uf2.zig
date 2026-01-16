const std = @import("std");

const Uf2Flag = packed struct {
    // not main flash
    // this block should be skipped when writing the device flash;
    // it can be used to store "comments" in the file, typically embedded source code
    // or debug info that does not fit on the device flash
    // NotMainFlash = 0x00000001,
    NotMainFlash: bool = false,
    _reserved0: u7 = 0,
    _reserved1: u4 = 0,
    // file container
    // FileContainer = 0x00001000,
    FileContainer: bool = false,
    // familyID present
    // when set, the fileSize/familyID holds a value identifying the board family
    // (usually corresponds to an MCU)
    // FamilyIDPresent = 0x00002000,
    FamilyIDPresent: bool = false,
    // MD5 checksum present
    // MD5ChecksumPresent = 0x00004000,
    MD5ChecksumPresent: bool = false,
    // extension tags present
    // ExtensionTagsPresent = 0x00008000,
    ExtensionTagsPresent: bool = false,
    _reserved2: u16 = 0,
};

const Uf2ExtTag = enum(u32) {
    // version of firmware file - UTF8 semver string
    Version = 0x9fc7bc00,
    // description of device for which the firmware file is destined (UTF8)
    Device = 0x650d9d00,
    // page size of target device (32 bit unsigned number)
    PageSize = 0x0be9f700,
    // SHA-2 checksum of firmware (can be of various size)
    SHA2Checksum = 0xb46db000,
    // device type identifier - a refinement of familyID meant to identify a kind of device
    // (eg., a toaster with specific pinout and heating unit), not only MCU;
    // 32 or 64 bit number; can be hash of 0x650d9d
    DeviceType = 0xc8a72900,
};

const Uf2Block = extern union {
    u8: [512]u8,
    s: extern struct {
        magicStart0: u32 = 0x0A324655,
        magicStart1: u32 = 0x9E5D5157,
        flags: Uf2Flag = .{},
        targetAddr: u32,
        payloadSize: u32,
        blockNo: u32,
        numBlocks: u32,
        field7: extern union {
            fileSize: u32,
            familyID: u32,
        },
        // NAND erase pattern
        data: [476]u8 = [_]u8{0xff} ** 476,
        magicEnd: u32 = 0x0AB16F30,
    },
};

pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();

    const alloc = arena.allocator();
    const args = try std.process.argsAlloc(alloc);

    // Parse args
    var option: ?[]const u8 = null;
    var base: ?u32 = null;
    var family_id: ?u32 = null;
    var input_path: ?[]const u8 = null;
    var output_path: ?[]const u8 = null;

    for (args[1..]) |arg| {
        const a = arg[0..arg.len];
        // std.debug.print("arg: {s}\n", .{a});
        if (std.mem.startsWith(u8, a, "-")) {
            if (option) |opt|
                fatal("Missing argument for: {s}", .{opt});
            option = a;
        } else if (option) |opt| {
            if (std.mem.eql(u8, opt, "-b")) {
                base = try std.fmt.parseInt(u32, a, 0);
                option = null;
            } else if (std.mem.eql(u8, opt, "-f")) {
                family_id = try std.fmt.parseInt(u32, a, 0);
                option = null;
            } else {
                fatal("Unexpected argument: {s}", .{opt});
            }
        } else {
            if (input_path == null) {
                input_path = a;
            } else if (output_path == null) {
                output_path = a;
            } else {
                fatal("Unexpected argument: {s}", .{a});
            }
        }
    }

    if (base == null)
        fatal("Base address not specified", .{});
    if (family_id == null)
        fatal("Family ID not specified", .{});
    if (input_path == null)
        fatal("Input file not specified", .{});
    if (output_path == null)
        fatal("Output file not specified", .{});

    var input_file = std.fs.cwd().openFile(input_path.?, .{ .mode = .read_only }) catch |err|
        fatal("unable to open '{s}': {s}", .{ input_path.?, @errorName(err) });
    defer input_file.close();

    var output_file = std.fs.cwd().createFile(output_path.?, .{}) catch |err|
        fatal("unable to open '{s}': {s}", .{ output_path.?, @errorName(err) });
    defer output_file.close();

    var input_buffer: [4096]u8 = undefined;
    var input_reader = input_file.reader(&input_buffer);
    const input_stat = try input_file.stat();
    const input_data = try input_reader.interface.readAlloc(alloc, input_stat.size);

    var output_buffer: [4096]u8 = undefined;
    var output_writer = output_file.writer(&output_buffer);

    const max_payload_size = @sizeOf(@FieldType(@FieldType(Uf2Block, "s"), "data"));
    const num_blocks = 1 + (input_data.len + max_payload_size) / max_payload_size;
    for (0..num_blocks) |block_no| {
        const start = max_payload_size * (@max(1, block_no) - 1);
        const end = @min(input_data.len, start + max_payload_size);
        const pld = input_data[start..end];

        var block: Uf2Block = .{ .s = .{
            .flags = .{
                .FamilyIDPresent = true,
            },
            .targetAddr = @intCast(base.? + start),
            .payloadSize = @intCast(pld.len),
            .blockNo = @intCast(block_no),
            .numBlocks = @intCast(num_blocks),
            .field7 = .{ .familyID = family_id.? },
        } };

        if (block_no == 0) {
            // First information block
            block.s.flags.NotMainFlash = true;
            block.s.flags.ExtensionTagsPresent = true;
            block.s.targetAddr = 0;
            block.s.payloadSize = std.mem.readInt(u32, pld[0..4], .little);
            @memset(&block.s.data, 0);
            // TODO Add extension tags
            @memcpy(block.s.data[0..block.s.payloadSize], pld[0..block.s.payloadSize]);
        } else {
            // Data block
            @memcpy(block.s.data[0..pld.len], pld);
        }
        _ = try output_writer.interface.write(&block.u8);
    }

    try output_writer.interface.flush();
    return std.process.cleanExit();
}

fn fatal(comptime format: []const u8, args: anytype) noreturn {
    std.debug.print(format, args);
    std.process.exit(1);
}
