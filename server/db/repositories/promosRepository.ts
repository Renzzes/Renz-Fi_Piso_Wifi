import { db } from "../connection.js";

export const promosRepository = {
  list: () => {
    return db
      .prepare(
        `SELECT id, name, coin, minutes, speed, devices, data_cap_mb FROM promo_rates ORDER BY coin`,
      )
      .all() as {
      id: number;
      name: string;
      coin: number;
      minutes: number;
      speed: number | null;
      devices: number | null;
      data_cap_mb: number | null;
    }[];
  },

  create: (input: {
    name: string;
    coin: number;
    minutes: number;
    speed?: number | null;
    devices?: number | null;
    data_cap_mb?: number | null;
  }) => {
    const { name, coin, minutes, speed, devices, data_cap_mb } = input;
    const result = db
      .prepare(
        `INSERT INTO promo_rates (name, coin, minutes, speed, devices, data_cap_mb)
         VALUES (?, ?, ?, ?, ?, ?)`,
      )
      .run(name, coin, minutes, speed ?? null, devices ?? 1, data_cap_mb ?? null);
    return { id: result.lastInsertRowid };
  },

  update: (input: {
    id: number;
    name: string;
    coin: number;
    minutes: number;
    speed?: number | null;
    devices?: number | null;
    data_cap_mb?: number | null;
  }) => {
    const { id, name, coin, minutes, speed, devices, data_cap_mb } = input;
    db.prepare(
      `UPDATE promo_rates
       SET name=?, coin=?, minutes=?, speed=?, devices=?, data_cap_mb=?
       WHERE id=?`,
    ).run(name, coin, minutes, speed ?? null, devices ?? 1, data_cap_mb ?? null, id);
  },

  deleteById: (id: number) => {
    db.prepare("DELETE FROM promo_rates WHERE id = ?").run(id);
  },
};
