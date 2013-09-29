function go(x)
  for i=1, x do
    lcd(string.format("%d",i), "")
    sleep(1000)
  end
end

go(10)
