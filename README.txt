Inventory Management System (C++ / SQLite)

Features
- Add products with unique SKUs
- List and search products
- Adjust stock and keep a movement history
- Record a sale as an order, deducting stock in one transaction
- View low-stock, inventory-value, and all-time-sales reports
- Data is stored in inventory.db in the program's current directory

Build on Windows with Visual Studio C++ and vcpkg
1. Install vcpkg and SQLite: vcpkg install sqlite3:x64-windows
2. Open a Visual Studio Developer Command Prompt.
3. Compile, replacing C:\\path\\to\\vcpkg with your vcpkg folder:

   cl /EHsc /std:c++17 /I C:\\path\\to\\vcpkg\\installed\\x64-windows\\include main.cpp /link /LIBPATH:C:\\path\\to\\vcpkg\\installed\\x64-windows\\lib sqlite3.lib

4. Run main.exe. The program creates inventory.db automatically.

Example usage
1) Add a product, for example SKU A101, name Notebook, price 2.50, stock 20, reorder level 5.
2) List or search for the product.
3) Record a sale using its displayed ID and quantity.
4) Open Reports to see low stock and totals.

This is a learning project. It is a single-user command-line application; it does not include
user accounts, tax, returns, supplier management, or multi-product orders.
