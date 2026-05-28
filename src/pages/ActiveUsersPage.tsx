import { Wifi, WifiOff } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import { Badge } from "@/components/ui/badge";
import { useActiveUsers, useDisconnectUser } from "@/hooks/api/useActiveUsers";

export default function ActiveUsersPage() {
  const { data: users = [] } = useActiveUsers();
  const disconnect = useDisconnectUser();

  const kick = (mac: string) => disconnect.mutate(mac);

  return (
    <div>
      <PageHeader title="Active Users" description={`${users.length} devices connected`} />
      <div className="rounded-md border bg-card overflow-x-auto">
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>Device</TableHead>
              <TableHead>MAC</TableHead>
              <TableHead>IP</TableHead>
              <TableHead>Remaining</TableHead>
              <TableHead className="text-right">Action</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {users.map((u) => (
              <TableRow key={u.mac}>
                <TableCell className="flex items-center gap-2">
                  <Wifi className="h-3.5 w-3.5 text-emerald-500" /> {u.device}
                </TableCell>
                <TableCell className="font-mono text-xs">{u.mac}</TableCell>
                <TableCell className="font-mono text-xs">{u.ip}</TableCell>
                <TableCell>
                  <Badge variant="secondary">{u.remaining}</Badge>
                </TableCell>
                <TableCell className="text-right">
                  <Button
                    size="sm"
                    variant="destructive"
                    className="h-7"
                    onClick={() => kick(u.mac)}
                  >
                    <WifiOff className="h-3.5 w-3.5" /> Disconnect
                  </Button>
                </TableCell>
              </TableRow>
            ))}
            {users.length === 0 && (
              <TableRow>
                <TableCell colSpan={5} className="text-center text-muted-foreground py-6 text-sm">
                  No active users
                </TableCell>
              </TableRow>
            )}
          </TableBody>
        </Table>
      </div>
    </div>
  );
}
