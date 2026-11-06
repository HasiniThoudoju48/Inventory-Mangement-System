#include <sqlite3.h>

#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

// A small command-line inventory app. SQLite creates inventory.db beside the
// executable the first time the program runs.
class InventoryApp {
    sqlite3* db = nullptr;

    bool execute(const char* sql) {
        char* error = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &error) == SQLITE_OK) return true;
        std::cerr << "Database error: " << (error ? error : "unknown error") << '\n';
        sqlite3_free(error);
        return false;
    }

    void initialize() {
        execute("PRAGMA foreign_keys = ON;");
        execute("CREATE TABLE IF NOT EXISTS products ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "sku TEXT NOT NULL UNIQUE, name TEXT NOT NULL,"
                "price REAL NOT NULL CHECK(price >= 0),"
                "quantity INTEGER NOT NULL DEFAULT 0 CHECK(quantity >= 0),"
                "reorder_level INTEGER NOT NULL DEFAULT 5 CHECK(reorder_level >= 0));");
        execute("CREATE TABLE IF NOT EXISTS orders ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, total REAL NOT NULL);");
        execute("CREATE TABLE IF NOT EXISTS order_items ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, order_id INTEGER NOT NULL REFERENCES orders(id),"
                "product_id INTEGER NOT NULL REFERENCES products(id), quantity INTEGER NOT NULL,"
                "unit_price REAL NOT NULL);");
        execute("CREATE TABLE IF NOT EXISTS stock_movements ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, product_id INTEGER NOT NULL REFERENCES products(id),"
                "change_amount INTEGER NOT NULL, reason TEXT NOT NULL,"
                "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);");
    }

    static bool bindText(sqlite3_stmt* s, int n, const std::string& value) {
        return sqlite3_bind_text(s, n, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
    }

    void addProduct() {
        std::string sku, name;
        double price; int quantity, reorder;
        std::cout << "SKU: "; std::getline(std::cin, sku);
        std::cout << "Name: "; std::getline(std::cin, name);
        std::cout << "Price: "; if (!(std::cin >> price)) { badInput(); return; }
        std::cout << "Starting quantity: "; if (!(std::cin >> quantity)) { badInput(); return; }
        std::cout << "Reorder level: "; if (!(std::cin >> reorder)) { badInput(); return; }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (sku.empty() || name.empty() || price < 0 || quantity < 0 || reorder < 0) {
            std::cout << "Values must be non-empty and numbers cannot be negative.\n"; return;
        }
        sqlite3_stmt* s = nullptr;
        const char* sql = "INSERT INTO products(sku,name,price,quantity,reorder_level) VALUES(?,?,?,?,?)";
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) { dbError(); return; }
        bindText(s, 1, sku); bindText(s, 2, name);
        sqlite3_bind_double(s, 3, price); sqlite3_bind_int(s, 4, quantity); sqlite3_bind_int(s, 5, reorder);
        if (sqlite3_step(s) == SQLITE_DONE) std::cout << "Product added.\n";
        else std::cout << "Could not add product (SKU may already exist).\n";
        sqlite3_finalize(s);
    }

    void listProducts(const std::string& search = "") {
        sqlite3_stmt* s = nullptr;
        const char* sql = search.empty()
            ? "SELECT id,sku,name,price,quantity,reorder_level FROM products ORDER BY name"
            : "SELECT id,sku,name,price,quantity,reorder_level FROM products WHERE sku LIKE ? OR name LIKE ? ORDER BY name";
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) { dbError(); return; }
        if (!search.empty()) {
            std::string pattern = "%" + search + "%";
            bindText(s, 1, pattern); bindText(s, 2, pattern);
        }
        std::cout << "\nID  SKU        Name                          Price     Qty  Reorder\n";
        std::cout << "------------------------------------------------------------------\n";
        while (sqlite3_step(s) == SQLITE_ROW) {
            std::cout << std::left << std::setw(4) << sqlite3_column_int(s, 0)
                      << std::setw(11) << sqlite3_column_text(s, 1)
                      << std::setw(30) << sqlite3_column_text(s, 2)
                      << std::right << std::fixed << std::setprecision(2) << std::setw(8) << sqlite3_column_double(s, 3)
                      << std::setw(6) << sqlite3_column_int(s, 4)
                      << std::setw(9) << sqlite3_column_int(s, 5) << '\n';
        }
        sqlite3_finalize(s);
    }

    void searchProducts() {
        std::string term; std::cout << "Search SKU or name: "; std::getline(std::cin, term);
        listProducts(term);
    }

    void adjustStock() {
        int id, change; std::string reason;
        std::cout << "Product ID: "; if (!(std::cin >> id)) { badInput(); return; }
        std::cout << "Change in quantity (+ restock, - remove): "; if (!(std::cin >> change)) { badInput(); return; }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Reason: "; std::getline(std::cin, reason);
        if (change == 0 || reason.empty()) { std::cout << "Change cannot be zero; reason is required.\n"; return; }
        execute("BEGIN IMMEDIATE;");
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db, "UPDATE products SET quantity=quantity+? WHERE id=? AND quantity+?>=0", -1, &s, nullptr);
        sqlite3_bind_int(s, 1, change); sqlite3_bind_int(s, 2, id); sqlite3_bind_int(s, 3, change);
        bool updated = sqlite3_step(s) == SQLITE_DONE && sqlite3_changes(db) == 1;
        sqlite3_finalize(s);
        if (!updated) { execute("ROLLBACK;"); std::cout << "Product not found or quantity would become negative.\n"; return; }
        sqlite3_prepare_v2(db, "INSERT INTO stock_movements(product_id,change_amount,reason) VALUES(?,?,?)", -1, &s, nullptr);
        sqlite3_bind_int(s, 1, id); sqlite3_bind_int(s, 2, change); bindText(s, 3, reason);
        bool logged = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        if (logged && execute("COMMIT;")) std::cout << "Stock updated.\n";
        else { execute("ROLLBACK;"); std::cout << "Update failed; changes rolled back.\n"; }
    }

    void createSale() {
        int id, quantity;
        std::cout << "Product ID: "; if (!(std::cin >> id)) { badInput(); return; }
        std::cout << "Quantity sold: "; if (!(std::cin >> quantity)) { badInput(); return; }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (quantity <= 0) { std::cout << "Quantity must be positive.\n"; return; }
        execute("BEGIN IMMEDIATE;");
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db, "SELECT price FROM products WHERE id=? AND quantity>=?", -1, &s, nullptr);
        sqlite3_bind_int(s, 1, id); sqlite3_bind_int(s, 2, quantity);
        if (sqlite3_step(s) != SQLITE_ROW) {
            sqlite3_finalize(s); execute("ROLLBACK;");
            std::cout << "Product not found or insufficient stock.\n"; return;
        }
        double price = sqlite3_column_double(s, 0); sqlite3_finalize(s);
        double total = price * quantity;
        sqlite3_prepare_v2(db, "INSERT INTO orders(total) VALUES(?)", -1, &s, nullptr);
        sqlite3_bind_double(s, 1, total);
        bool ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        sqlite3_int64 orderId = sqlite3_last_insert_rowid(db);
        if (ok) {
            sqlite3_prepare_v2(db, "INSERT INTO order_items(order_id,product_id,quantity,unit_price) VALUES(?,?,?,?)", -1, &s, nullptr);
            sqlite3_bind_int64(s, 1, orderId); sqlite3_bind_int(s, 2, id); sqlite3_bind_int(s, 3, quantity); sqlite3_bind_double(s, 4, price);
            ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        }
        if (ok) {
            sqlite3_prepare_v2(db, "UPDATE products SET quantity=quantity-? WHERE id=? AND quantity>=?", -1, &s, nullptr);
            sqlite3_bind_int(s, 1, quantity); sqlite3_bind_int(s, 2, id); sqlite3_bind_int(s, 3, quantity);
            ok = sqlite3_step(s) == SQLITE_DONE && sqlite3_changes(db) == 1; sqlite3_finalize(s);
        }
        if (ok) {
            sqlite3_prepare_v2(db, "INSERT INTO stock_movements(product_id,change_amount,reason) VALUES(?,?,'Sale')", -1, &s, nullptr);
            sqlite3_bind_int(s, 1, id); sqlite3_bind_int(s, 2, -quantity);
            ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        }
        if (ok && execute("COMMIT;")) std::cout << "Sale recorded. Order #" << orderId << ", total $" << std::fixed << std::setprecision(2) << total << "\n";
        else { execute("ROLLBACK;"); std::cout << "Sale failed; changes rolled back.\n"; }
    }

    void reports() {
        std::cout << "\nLow-stock products (quantity <= reorder level):\n";
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db, "SELECT sku,name,quantity,reorder_level FROM products WHERE quantity<=reorder_level ORDER BY quantity", -1, &s, nullptr);
        bool any = false;
        while (sqlite3_step(s) == SQLITE_ROW) {
            any = true;
            std::cout << reinterpret_cast<const char*>(sqlite3_column_text(s,0)) << " | "
                      << reinterpret_cast<const char*>(sqlite3_column_text(s,1)) << " | qty "
                      << sqlite3_column_int(s,2) << " (reorder at " << sqlite3_column_int(s,3) << ")\n";
        }
        if (!any) std::cout << "None\n";
        sqlite3_finalize(s);
        sqlite3_prepare_v2(db, "SELECT COALESCE(SUM(price*quantity),0) FROM products", -1, &s, nullptr);
        sqlite3_step(s); std::cout << "Current inventory value: $" << std::fixed << std::setprecision(2) << sqlite3_column_double(s,0) << '\n';
        sqlite3_finalize(s);
        sqlite3_prepare_v2(db, "SELECT COALESCE(SUM(total),0) FROM orders", -1, &s, nullptr);
        sqlite3_step(s); std::cout << "All-time sales total: $" << std::fixed << std::setprecision(2) << sqlite3_column_double(s,0) << '\n';
        sqlite3_finalize(s);
    }

    void badInput() {
        std::cin.clear(); std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Please enter a valid number.\n";
    }
    void dbError() { std::cerr << "Database error: " << sqlite3_errmsg(db) << '\n'; }

public:
    bool open() {
        if (sqlite3_open("inventory.db", &db) != SQLITE_OK) { dbError(); return false; }
        initialize(); return true;
    }
    ~InventoryApp() { if (db) sqlite3_close(db); }

    void run() {
        while (true) {
            std::cout << "\n=== Inventory Manager ===\n"
                      << "1. Add product\n2. List products\n3. Search products\n"
                      << "4. Adjust stock\n5. Record sale\n6. Reports\n0. Exit\nChoice: ";
            int choice;
            if (!(std::cin >> choice)) { if (std::cin.eof()) break; badInput(); continue; }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            switch (choice) {
                case 1: addProduct(); break;
                case 2: listProducts(); break;
                case 3: searchProducts(); break;
                case 4: adjustStock(); break;
                case 5: createSale(); break;
                case 6: reports(); break;
                case 0: return;
                default: std::cout << "Unknown option.\n";
            }
        }
    }
};

int main() {
    InventoryApp app;
    if (!app.open()) return 1;
    app.run();
}
