config(colors, false)

function go(x)
  for i=1, x do
    lcd(string.format("  %03d  ",i), "")
    print(i);
    sleep(500)
  end
end

go(10)
