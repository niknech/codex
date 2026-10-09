ALTER TABLE entries ADD COLUMN category TEXT NOT NULL DEFAULT 'unspecified'
    CHECK(category IN ('unspecified','breakfast','lunch','dinner'));
ALTER TABLE drafts ADD COLUMN category TEXT NOT NULL DEFAULT 'unspecified'
    CHECK(category IN ('unspecified','breakfast','lunch','dinner'));
INSERT INTO schema_migrations VALUES(2,unixepoch());
