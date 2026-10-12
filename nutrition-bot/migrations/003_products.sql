CREATE TABLE products(user INTEGER NOT NULL REFERENCES users(id), barcode TEXT NOT NULL, data TEXT NOT NULL, updated INTEGER NOT NULL, PRIMARY KEY(user,barcode));
CREATE TABLE ai_bonus(user INTEGER NOT NULL REFERENCES users(id), day TEXT NOT NULL, amount INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(user,day));
CREATE TABLE product_jobs(id INTEGER PRIMARY KEY, user INTEGER NOT NULL REFERENCES users(id), flow INTEGER NOT NULL, kind TEXT NOT NULL, payload TEXT NOT NULL, day TEXT NOT NULL, paid INTEGER NOT NULL DEFAULT 0, status TEXT NOT NULL DEFAULT 'pending');
CREATE INDEX product_jobs_pending ON product_jobs(status,id);
INSERT INTO schema_migrations VALUES(3,unixepoch());
