import { ChevronLeft, ChevronRight } from "lucide-react";
import { Button } from "@/components/ui/button";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { PAGE_SIZES } from "@/lib/pagination";
import { cn } from "@/lib/utils";

export function DataPagination({
  page,
  pageSize,
  total,
  onPageChange,
  onPageSizeChange,
  itemLabel = "entries",
  pageSizes = PAGE_SIZES,
}: {
  page: number;
  pageSize: number;
  total: number;
  onPageChange: (page: number) => void;
  onPageSizeChange: (size: number) => void;
  itemLabel?: string;
  pageSizes?: readonly number[];
}) {
  const totalPages = Math.max(1, Math.ceil(total / pageSize) || 1);
  const from = total === 0 ? 0 : (page - 1) * pageSize + 1;
  const to = Math.min(page * pageSize, total);
  const pages = visiblePages(page, totalPages);

  return (
    <div className="flex flex-col gap-2 px-3 py-2 sm:flex-row sm:items-center sm:justify-between">
      <p className="text-[12px] text-muted-foreground">
        Showing {from} to {to} of {total} {itemLabel}
      </p>
      <div className="flex flex-wrap items-center gap-1.5">
        <Button
          type="button"
          variant="outline"
          size="icon"
          className="h-8 w-8"
          disabled={page <= 1}
          aria-label="Previous page"
          onClick={() => onPageChange(page - 1)}
        >
          <ChevronLeft className="h-4 w-4" />
        </Button>
        {pages.map((item, index) =>
          item === "ellipsis" ? (
            <span key={`e-${index}`} className="px-1 text-[12px] text-muted-foreground">
              …
            </span>
          ) : (
            <Button
              key={item}
              type="button"
              variant={item === page ? "default" : "outline"}
              size="icon"
              className={cn(
                "h-8 w-8 text-[12px]",
                item === page && "bg-primary text-primary-foreground",
              )}
              aria-label={`Page ${item}`}
              aria-current={item === page ? "page" : undefined}
              onClick={() => onPageChange(item)}
            >
              {item}
            </Button>
          ),
        )}
        <Button
          type="button"
          variant="outline"
          size="icon"
          className="h-8 w-8"
          disabled={page >= totalPages}
          aria-label="Next page"
          onClick={() => onPageChange(page + 1)}
        >
          <ChevronRight className="h-4 w-4" />
        </Button>
        <Select value={String(pageSize)} onValueChange={(value) => onPageSizeChange(Number(value))}>
          <SelectTrigger className="h-8 w-[108px] text-[12px]" aria-label="Rows per page">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            {pageSizes.map((size) => (
              <SelectItem key={size} value={String(size)}>
                {size} / page
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
      </div>
    </div>
  );
}

function visiblePages(page: number, totalPages: number): Array<number | "ellipsis"> {
  if (totalPages <= 7) {
    return Array.from({ length: totalPages }, (_, i) => i + 1);
  }
  const items: Array<number | "ellipsis"> = [1];
  const start = Math.max(2, page - 1);
  const end = Math.min(totalPages - 1, page + 1);
  if (start > 2) items.push("ellipsis");
  for (let i = start; i <= end; i += 1) items.push(i);
  if (end < totalPages - 1) items.push("ellipsis");
  items.push(totalPages);
  return items;
}
