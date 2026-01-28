#!/usr/bin/env python3
"""
XApp Status Icon Report Tool

This script queries D-Bus for all XAppStatusIcon instances and displays
detailed information about each one, including whether they're backed by
StatusNotifier (SNI) items or are native XAppStatusIcons.
"""

import sys
import json
import argparse
from gi.repository import Gio, GLib

# ANSI color codes
class Colors:
    RESET = '\033[0m'
    BOLD = '\033[1m'
    DIM = '\033[2m'
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'

    @staticmethod
    def disable():
        Colors.RESET = ''
        Colors.BOLD = ''
        Colors.DIM = ''
        Colors.HEADER = ''
        Colors.BLUE = ''
        Colors.CYAN = ''
        Colors.GREEN = ''
        Colors.YELLOW = ''
        Colors.RED = ''

XAPP_ICON_BASE_PATH = "/org/x/StatusIcon"
XAPP_ICON_INTERFACE = "org.x.StatusIcon"
SNW_BUS_NAME = "org.kde.StatusNotifierWatcher"
SNW_OBJECT_PATH = "/StatusNotifierWatcher"
SNW_INTERFACE = "org.kde.StatusNotifierWatcher"

class StatusIconReporter:
    def __init__(self, use_colors=True, format_style='table'):
        self.connection = None
        self.icons = []
        self.sni_items = []
        self.format_style = format_style

        if not use_colors:
            Colors.disable()

    def connect_to_bus(self):
        """Connect to the session bus."""
        try:
            self.connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
            return True
        except GLib.Error as e:
            print(f"ERROR: Could not connect to session bus: {e.message}")
            return False

    def get_sni_registered_items(self):
        """Get list of registered StatusNotifier items from the watcher."""
        try:
            proxy = Gio.DBusProxy.new_sync(
                self.connection,
                Gio.DBusProxyFlags.NONE,
                None,
                SNW_BUS_NAME,
                SNW_OBJECT_PATH,
                SNW_INTERFACE,
                None
            )

            variant = proxy.get_cached_property("RegisteredStatusNotifierItems")
            if variant:
                self.sni_items = variant.unpack()
                return True
            return False
        except GLib.Error:
            return False

    def introspect_path(self, bus_name, path):
        """Introspect a D-Bus path to find child objects."""
        try:
            result = self.connection.call_sync(
                bus_name,
                path,
                "org.freedesktop.DBus.Introspectable",
                "Introspect",
                None,
                GLib.VariantType.new("(s)"),
                Gio.DBusCallFlags.NONE,
                5000,
                None
            )

            xml_data = result.unpack()[0]

            import xml.etree.ElementTree as ET
            root = ET.fromstring(xml_data)

            nodes = []
            for node in root.findall('node'):
                name = node.get('name')
                if name and not name.startswith(':'):
                    nodes.append(name)

            return nodes
        except GLib.Error:
            return []

    def find_all_icon_objects(self):
        """Find all XAppStatusIcon objects by introspecting the bus."""
        icon_objects = []

        try:
            result = self.connection.call_sync(
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus",
                "ListNames",
                None,
                GLib.VariantType.new("(as)"),
                Gio.DBusCallFlags.NONE,
                5000,
                None
            )

            names = result.unpack()[0]

            for bus_name in names:
                if bus_name.startswith("org.x.StatusIcon"):
                    try:
                        nodes = self.introspect_path(bus_name, XAPP_ICON_BASE_PATH)

                        for node in nodes:
                            object_path = f"{XAPP_ICON_BASE_PATH}/{node}"
                            icon_objects.append((bus_name, object_path))

                    except Exception:
                        continue

        except GLib.Error as e:
            print(f"ERROR: Could not list bus names: {e.message}")

        return icon_objects

    def get_icon_properties(self, bus_name, object_path):
        """Get all properties for an XAppStatusIcon object."""
        try:
            proxy = Gio.DBusProxy.new_sync(
                self.connection,
                Gio.DBusProxyFlags.NONE,
                None,
                bus_name,
                object_path,
                XAPP_ICON_INTERFACE,
                None
            )

            properties = {}
            property_names = [
                "Name", "IconName", "TooltipText", "Label",
                "Visible", "IconSize", "PrimaryMenuIsOpen",
                "SecondaryMenuIsOpen", "Metadata"
            ]

            for prop_name in property_names:
                try:
                    variant = proxy.get_cached_property(prop_name)
                    if variant:
                        properties[prop_name] = variant.unpack()
                    else:
                        properties[prop_name] = None
                except:
                    properties[prop_name] = None

            return properties

        except GLib.Error as e:
            return {"error": str(e)}

    def is_sni_backed(self, bus_name):
        """Determine if an icon is backed by a StatusNotifier item.

        Icons created by xapp-sn-watcher have a bus name containing 'xapp_sn_watcher'.
        """
        return "xapp_sn_watcher" in bus_name

    def collect_icon_info(self):
        """Collect information about all status icons."""
        print("Scanning for XAppStatusIcons...")

        icon_objects = self.find_all_icon_objects()

        if not icon_objects:
            print("No XAppStatusIcon instances found.")
            return

        print(f"Found {len(icon_objects)} icon object(s).\n")

        for bus_name, object_path in icon_objects:
            properties = self.get_icon_properties(bus_name, object_path)
            is_sni = self.is_sni_backed(bus_name)

            icon_info = {
                "bus_name": bus_name,
                "object_path": object_path,
                "is_sni_backed": is_sni,
                "properties": properties
            }

            self.icons.append(icon_info)

    def strip_ansi(self, text):
        """Remove ANSI escape codes from text."""
        import re
        return re.sub(r'\033\[[0-9;]*m', '', str(text))

    def visible_length(self, text):
        """Get the visible length of text (excluding ANSI codes)."""
        return len(self.strip_ansi(text))

    def pad_to_width(self, text, width):
        """Pad text to specified width, accounting for ANSI codes."""
        visible_len = self.visible_length(text)

        if visible_len > width:
            # Truncate
            stripped = self.strip_ansi(text)
            return stripped[:width-2] + '..'

        # Add padding
        padding = width - visible_len
        return text + (' ' * padding)

    def print_table_row(self, cols, widths):
        """Print a table row with proper column widths."""
        formatted_cols = []
        for col, width in zip(cols, widths):
            col_str = str(col) if col is not None else ''
            formatted_cols.append(self.pad_to_width(col_str, width))

        return '│ ' + ' │ '.join(formatted_cols) + ' │'

    def print_table_separator(self, widths):
        """Print a table separator line."""
        parts = ['─' * w for w in widths]
        return '├─' + '─┼─'.join(parts) + '─┤'

    def print_table_border(self, widths, style='top'):
        """Print a table border."""
        parts = ['─' * w for w in widths]
        if style == 'top':
            return '┌─' + '─┬─'.join(parts) + '─┐'
        else:  # bottom
            return '└─' + '─┴─'.join(parts) + '─┘'

    def print_report_table(self):
        """Print report in table format."""
        if not self.icons:
            return

        # Print header
        print(f"\n{Colors.BOLD}{Colors.HEADER}╔{'═' * 78}╗{Colors.RESET}")
        print(f"{Colors.BOLD}{Colors.HEADER}║{' ' * 24}XAPP STATUS ICON REPORT{' ' * 31}║{Colors.RESET}")
        print(f"{Colors.BOLD}{Colors.HEADER}╚{'═' * 78}╝{Colors.RESET}\n")

        # Summary
        native_count = sum(1 for icon in self.icons if not icon['is_sni_backed'])
        sni_count = sum(1 for icon in self.icons if icon['is_sni_backed'])

        print(f"{Colors.BOLD}Summary:{Colors.RESET}")
        print(f"  Total Icons: {Colors.CYAN}{len(self.icons)}{Colors.RESET}")
        print(f"  ├─ {Colors.GREEN}Native XAppStatusIcons:{Colors.RESET} {native_count}")
        print(f"  └─ {Colors.YELLOW}StatusNotifier-backed:{Colors.RESET} {sni_count}")
        print()

        # SNI items if present
        if self.sni_items:
            print(f"{Colors.BOLD}Registered StatusNotifier Items:{Colors.RESET}")
            for item in self.sni_items:
                print(f"  • {Colors.DIM}{item}{Colors.RESET}")
            print()

        # Table header
        print(f"{Colors.BOLD}Status Icons:{Colors.RESET}\n")

        # Calculate dynamic column widths
        # Start with minimum widths
        widths = [3, 20, 15, 8, 6, 30]
        headers = ['#', 'Name', 'Type', 'Vis', 'Size', 'Icon Name']

        # Adjust based on actual data
        for icon in self.icons:
            props = icon['properties']
            name_len = len(props.get('Name', ''))
            icon_len = len(props.get('IconName', ''))

            widths[1] = max(widths[1], name_len + 2)
            widths[5] = max(widths[5], min(icon_len + 2, 40))  # Cap at 40

        # Top border
        print(self.print_table_border(widths, 'top'))

        # Headers
        header_row = self.print_table_row([f"{Colors.BOLD}{h}{Colors.RESET}" for h in headers], widths)
        print(header_row)

        # Separator
        print(self.print_table_separator(widths))

        # Data rows
        for idx, icon in enumerate(self.icons, 1):
            props = icon['properties']

            if icon['is_sni_backed']:
                type_str = f"{Colors.YELLOW}SNI-backed{Colors.RESET}"
            else:
                type_str = f"{Colors.GREEN}Native{Colors.RESET}"

            visible = props.get('Visible', False)
            visible_str = f"{Colors.GREEN}✓{Colors.RESET}" if visible else f"{Colors.DIM}✗{Colors.RESET}"

            size = props.get('IconSize', 0)
            size_str = str(size) if size > 0 else f"{Colors.DIM}N/A{Colors.RESET}"

            name = props.get('Name', 'N/A')
            icon_name = props.get('IconName', 'N/A')

            cols = [
                f"{Colors.DIM}{idx}{Colors.RESET}",
                name,
                type_str,
                visible_str,
                size_str,
                icon_name
            ]

            print(self.print_table_row(cols, widths))

        # Bottom border
        print(self.print_table_border(widths, 'bottom'))
        print()

    def print_report_detailed(self):
        """Print detailed report for each icon."""
        for idx, icon in enumerate(self.icons, 1):
            print(f"\n{Colors.BOLD}{Colors.CYAN}╭{'─' * 78}╮{Colors.RESET}")
            print(f"{Colors.BOLD}{Colors.CYAN}│{Colors.RESET} {Colors.BOLD}ICON #{idx}{Colors.RESET}{' ' * 71}{Colors.CYAN}│{Colors.RESET}")
            print(f"{Colors.BOLD}{Colors.CYAN}╰{'─' * 78}╯{Colors.RESET}\n")

            # Type indication
            if icon['is_sni_backed']:
                type_color = Colors.YELLOW
                type_label = "StatusNotifier-backed (xapp-sn-watcher proxy)"
            else:
                type_color = Colors.GREEN
                type_label = "Native XAppStatusIcon"

            print(f"{Colors.BOLD}D-Bus Name:{Colors.RESET}  {Colors.CYAN}{icon['bus_name']}{Colors.RESET}")
            print(f"{Colors.BOLD}Object Path:{Colors.RESET} {Colors.DIM}{icon['object_path']}{Colors.RESET}")
            print(f"{Colors.BOLD}Type:{Colors.RESET}        {type_color}{type_label}{Colors.RESET}")
            print()

            if 'error' in icon['properties']:
                print(f"{Colors.RED}ERROR: {icon['properties']['error']}{Colors.RESET}")
            else:
                props = icon['properties']

                # Properties in a nice format
                print(f"{Colors.BOLD}Properties:{Colors.RESET}")
                print(f"  ├─ Name:           {props.get('Name', Colors.DIM + 'N/A' + Colors.RESET)}")
                print(f"  ├─ Icon Name:      {props.get('IconName', Colors.DIM + 'N/A' + Colors.RESET)}")

                tooltip = props.get('TooltipText', '')
                if tooltip:
                    print(f"  ├─ Tooltip:        {tooltip}")
                else:
                    print(f"  ├─ Tooltip:        {Colors.DIM}(none){Colors.RESET}")

                label = props.get('Label', '')
                if label:
                    print(f"  ├─ Label:          {label}")
                else:
                    print(f"  ├─ Label:          {Colors.DIM}(none){Colors.RESET}")

                visible = props.get('Visible', False)
                visible_str = f"{Colors.GREEN}Yes{Colors.RESET}" if visible else f"{Colors.DIM}No{Colors.RESET}"
                print(f"  ├─ Visible:        {visible_str}")

                size = props.get('IconSize', 0)
                size_str = str(size) if size > 0 else f"{Colors.DIM}N/A{Colors.RESET}"
                print(f"  ├─ Icon Size:      {size_str}")

                primary_open = props.get('PrimaryMenuIsOpen', False)
                primary_str = f"{Colors.GREEN}Open{Colors.RESET}" if primary_open else f"{Colors.DIM}Closed{Colors.RESET}"
                print(f"  ├─ Primary Menu:   {primary_str}")

                secondary_open = props.get('SecondaryMenuIsOpen', False)
                secondary_str = f"{Colors.GREEN}Open{Colors.RESET}" if secondary_open else f"{Colors.DIM}Closed{Colors.RESET}"
                print(f"  ├─ Secondary Menu: {secondary_str}")

                metadata = props.get('Metadata')
                if metadata:
                    try:
                        metadata_obj = json.loads(metadata)
                        metadata_str = json.dumps(metadata_obj, indent=2)
                        # Indent the JSON
                        metadata_lines = metadata_str.split('\n')
                        print(f"  └─ Metadata:")
                        for line in metadata_lines:
                            print(f"      {Colors.DIM}{line}{Colors.RESET}")
                    except:
                        print(f"  └─ Metadata:       {Colors.DIM}{metadata}{Colors.RESET}")
                else:
                    print(f"  └─ Metadata:       {Colors.DIM}(none){Colors.RESET}")

    def print_report(self):
        """Print a formatted report of all status icons."""
        if not self.icons:
            return

        if self.format_style == 'table':
            self.print_report_table()
        elif self.format_style == 'detailed':
            self.print_report_table()
            print(f"\n{Colors.BOLD}{Colors.HEADER}{'─' * 80}{Colors.RESET}")
            print(f"{Colors.BOLD}{Colors.HEADER}DETAILED VIEW{Colors.RESET}")
            print(f"{Colors.BOLD}{Colors.HEADER}{'─' * 80}{Colors.RESET}")
            self.print_report_detailed()
        elif self.format_style == 'full':
            self.print_report_detailed()

    def run(self):
        """Main entry point for the reporter."""
        if not self.connect_to_bus():
            return 1

        self.get_sni_registered_items()
        self.collect_icon_info()
        self.print_report()

        return 0

def main():
    parser = argparse.ArgumentParser(
        description='Report on XAppStatusIcon instances on D-Bus',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Format styles:
  table     - Compact table overview (default)
  detailed  - Table overview + detailed per-icon information
  full      - Only detailed per-icon information

Examples:
  %(prog)s                    # Show table overview
  %(prog)s -f detailed        # Show table + details
  %(prog)s --no-color         # Disable colors
        """
    )

    parser.add_argument('-f', '--format',
                        choices=['table', 'detailed', 'full'],
                        default='table',
                        help='Output format style (default: table)')

    parser.add_argument('--no-color',
                        action='store_true',
                        help='Disable colored output')

    args = parser.parse_args()

    reporter = StatusIconReporter(
        use_colors=not args.no_color,
        format_style=args.format
    )
    return reporter.run()

if __name__ == "__main__":
    sys.exit(main())
