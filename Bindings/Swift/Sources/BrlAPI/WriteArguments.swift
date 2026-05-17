//
// libbrlapi — write helpers.
//

import CBrlAPI
import Foundation

extension BrlAPI.Connection {

    /// Cursor position used by `writeRegion`. Cells are 1-indexed to match the
    /// brlapi convention; `.off` clears the cursor; `.leave` doesn't touch it.
    public enum Cursor: Equatable {
        case leave
        case off
        case cell(Int)

        fileprivate var raw: Int32 {
            switch self {
            case .leave: return Int32(BRLAPI_CURSOR_LEAVE)
            case .off:   return Int32(BRLAPI_CURSOR_OFF)
            case .cell(let c): return Int32(c)
            }
        }
    }

    /// Update a single region of the display with text. This is the most
    /// common "write" call; if you need attribute masks or non-default
    /// charsets, drop down to the closure-based `write(_:)` API.
    ///
    /// - Parameters:
    ///   - text: characters to display in the region.
    ///   - begin: 1-based index of the first cell in the region.
    ///   - size: number of cells the region spans. Pass `nil` to let
    ///     libbrlapi infer from `text.count`.
    ///   - cursor: cursor placement, defaulting to `.leave` (unchanged).
    public func writeRegion(_ text: String,
                            begin: Int = 1,
                            size: Int? = nil,
                            cursor: Cursor = .leave) throws {
        try text.withCString { textPtr in
            try write { args in
                args.regionBegin = UInt32(begin)
                args.regionSize = Int32(size ?? text.utf8.count)
                args.text = textPtr
                args.textSize = Int32(text.utf8.count)
                args.cursor = cursor.raw
            }
        }
    }

    /// Clear the display by writing spaces across the whole geometry. Useful
    /// when leaving tty mode or transitioning between UI states.
    public func clear() throws {
        let size = displaySize
        let cells = size.totalCells
        if cells == 0 { return }
        let blanks = String(repeating: " ", count: cells)
        try writeRegion(blanks, begin: 1, size: cells, cursor: .off)
    }
}
