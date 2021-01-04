use usmsc;
drop procedure if exists drop_all_foreign_keys;
delimiter $$
create procedure drop_all_foreign_keys()
begin
declare done int default 0;
declare v_drop_sql varchar(1000);
declare cur_drop cursor for select concat('alter table ',TABLE_SCHEMA,'.',TABLE_NAME,' drop foreign key ',CONSTRAINT_NAME,' ;') from information_schema.TABLE_CONSTRAINTS where TABLE_SCHEMA in ('usmsc','usmschis','activiti') and CONSTRAINT_TYPE  in ('FOREIGN KEY');
declare continue handler for not found set done=1;
open cur_drop;
preloop:loop
fetch cur_drop into v_drop_sql;
if done = 1 then 	
	leave preloop;
end if;
select v_drop_sql;
set @v_drop_sql = v_drop_sql;
prepare v_sql from @v_drop_sql;
execute v_sql;
deallocate prepare v_sql;
end loop;
close cur_drop;
end$$
delimiter ;
select 'drop_all_foreign_keys',sysdate();
call drop_all_foreign_keys();